#ifdef SAM_UI_CAPTURE

#include "app/ui_capture.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <crtdbg.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>

#include <facet/core/anim.h>
#include <facet/core/context.h>
#include <facet/core/scale.h>
#include <facet/core/text.h>
#include <facet/core/theme.h>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#pragma warning(push, 0)
#pragma warning(disable : 4996)
#include <stb_image_write.h>
#pragma warning(pop)

#include "app/app_state.hpp"
#include "app/job_pump.hpp"
#include "app/ui_capture_internal.hpp"
#include "app/vault_registry.hpp"
#include "core/http/client.hpp"
#include "core/log.hpp"
#include "core/time_aligner.hpp"
#include "platform/paths.hpp"
#include "ui/fonts.hpp"
#include "ui/icons.hpp"
#include "ui/kit/transient.hpp"
#include "ui/main_window.hpp"
#include "ui/theme.hpp"
#include "ui/widgets/avatar_cache.hpp"
#include "ui/widgets/rank_image.hpp"

using Microsoft::WRL::ComPtr;

namespace sam::app::ui_capture {

namespace {

constexpr float kTimeStep = 1.0F / 60.0F;

std::vector<CaptureScene>& registered_scenes() {
    static std::vector<CaptureScene> scenes;
    return scenes;
}

struct Options {
    std::filesystem::path out_dir;
    std::string scene;
    bool light = true;
    bool dark = true;
    int width = 1280;
    int height = 800;
    float scale = 1.0F;
    bool gpu = false;
};

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string path_utf8(const std::filesystem::path& p) {
    return to_utf8(p.wstring());
}

void bind_console() {
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err != nullptr && err != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}

int fail(const std::string& message) {
    std::fprintf(stderr, "ui-capture: error: %s\n", message.c_str());
    std::fflush(stderr);
    SAM_LOG_ERROR("ui-capture: {}", message);
    return 1;
}

// A capture run has nobody in front of it. A failed assertion must end the run with its text
// on stderr; a dialog would sit on the desktop and block the run and everything queued behind
// it until someone clicks.
void make_failures_non_interactive() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (const int kind : {_CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(kind, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(kind, _CRTDBG_FILE_STDERR);
    }
}

// Dear ImGui's recoverable usage errors (an unbalanced Begin/End, SetCursorPos past the
// window end without an item after it, ...). The harness turns their assert off and collects
// them here, so the scene still renders and the run reports each one with its window.
void on_imgui_usage_error(ImGuiContext* ctx, void* user_data, const char* msg) {
    auto* errors = static_cast<std::vector<std::string>*>(user_data);
    const char* window = (ctx != nullptr && ctx->CurrentWindow != nullptr) ? ctx->CurrentWindow->Name : "?";
    std::string text = std::string("in window '") + window + "': " + (msg != nullptr ? msg : "");
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (std::find(errors->begin(), errors->end(), text) == errors->end()) errors->push_back(std::move(text));
}

bool parse_options(Options& opt, std::string& error) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        error = "cannot parse the command line";
        return false;
    }
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);

    bool have_dir = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        const bool has_value = i + 1 < args.size();
        if (a == L"--ui-capture" && has_value) {
            opt.out_dir = std::filesystem::absolute(args[++i]);
            have_dir = true;
        } else if (a == L"--scene" && has_value) {
            opt.scene = to_utf8(args[++i]);
        } else if (a == L"--theme" && has_value) {
            const std::wstring t = args[++i];
            if (t == L"light") {
                opt.light = true;
                opt.dark = false;
            } else if (t == L"dark") {
                opt.light = false;
                opt.dark = true;
            } else if (t == L"both") {
                opt.light = opt.dark = true;
            } else {
                error = "--theme takes light, dark or both";
                return false;
            }
        } else if (a == L"--size" && has_value) {
            int w = 0;
            int h = 0;
            if (std::swscanf(args[++i].c_str(), L"%dx%d", &w, &h) != 2 || w < 64 || h < 64 || w > 8192 ||
                h > 8192) {
                error = "--size takes WIDTHxHEIGHT, each 64 to 8192";
                return false;
            }
            opt.width = w;
            opt.height = h;
        } else if (a == L"--scale" && has_value) {
            const float s = std::wcstof(args[++i].c_str(), nullptr);
            if (!(s >= 0.5F && s <= 4.0F)) {
                error = "--scale takes a number from 0.5 to 4";
                return false;
            }
            opt.scale = s;
        } else if (a == L"--gpu") {
            opt.gpu = true;
        } else {
            error = "unknown or incomplete argument: " + to_utf8(a);
            return false;
        }
    }
    if (!have_dir) {
        error = "usage: --ui-capture <outdir> [--scene <id>] [--theme light|dark|both] "
                "[--size 1280x800] [--scale 1.0] [--gpu]";
        return false;
    }
    return true;
}

std::vector<CaptureScene> all_scenes() {
    std::vector<CaptureScene> scenes = builtin_scenes();
    std::vector<CaptureScene> extra = registered_scenes();
    std::sort(extra.begin(), extra.end(),
              [](const CaptureScene& a, const CaptureScene& b) { return std::strcmp(a.id, b.id) < 0; });
    for (const CaptureScene& s : extra) {
        const bool taken = std::any_of(scenes.begin(), scenes.end(),
                                       [&](const CaptureScene& t) { return std::strcmp(t.id, s.id) == 0; });
        if (!taken) scenes.push_back(s);
    }
    return scenes;
}

struct Gpu {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> view;
    ComPtr<ID3D11Texture2D> staging;
};

bool create_gpu(Gpu& gpu, const Options& opt, std::string& error) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    const D3D_DRIVER_TYPE type = opt.gpu ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
    HRESULT hr = D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                   gpu.device.GetAddressOf(), nullptr, gpu.context.GetAddressOf());
    if (FAILED(hr)) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "D3D11CreateDevice failed (0x%08lX)", static_cast<unsigned long>(hr));
        error = buf;
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(opt.width);
    desc.Height = static_cast<UINT>(opt.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = gpu.device->CreateTexture2D(&desc, nullptr, gpu.target.GetAddressOf());
    if (SUCCEEDED(hr)) hr = gpu.device->CreateRenderTargetView(gpu.target.Get(), nullptr, gpu.view.GetAddressOf());
    if (SUCCEEDED(hr)) {
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = gpu.device->CreateTexture2D(&desc, nullptr, gpu.staging.GetAddressOf());
    }
    if (FAILED(hr)) {
        error = "cannot create the offscreen render target";
        return false;
    }
    return true;
}

bool read_pixels(Gpu& gpu, const Options& opt, std::vector<unsigned char>& rgba) {
    gpu.context->CopyResource(gpu.staging.Get(), gpu.target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu.context->Map(gpu.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const std::size_t row = static_cast<std::size_t>(opt.width) * 4;
    rgba.resize(row * static_cast<std::size_t>(opt.height));
    const auto* src = static_cast<const unsigned char*>(mapped.pData);
    for (int y = 0; y < opt.height; ++y) {
        std::memcpy(rgba.data() + row * static_cast<std::size_t>(y),
                    src + static_cast<std::size_t>(mapped.RowPitch) * static_cast<std::size_t>(y), row);
    }
    gpu.context->Unmap(gpu.staging.Get(), 0);
    for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    return true;
}

void render_frame(Gpu& gpu, AppState& state) {
    ImGui::GetIO().DeltaTime = kTimeStep;
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    facet::NewFrame();
    job_pump::drain(state);
    ui::draw(state);
    facet::EndFrame();
    ImGui::Render();

    const ImVec4& bg = facet::T().colors.bg;
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0F};
    ID3D11RenderTargetView* views[] = {gpu.view.Get()};
    gpu.context->OMSetRenderTargets(1, views, nullptr);
    gpu.context->ClearRenderTargetView(gpu.view.Get(), clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void apply_capture_look(const Settings& settings, bool dark) {
    Settings look = settings;
    look.theme_mode = dark ? ThemeMode::Dark : ThemeMode::Light;
    ui::theme::apply_appearance(look, false);
}

bool capture_one(Gpu& gpu, const Options& opt, const CaptureScene& scene, bool dark,
                 std::vector<unsigned char>& pixels, std::string& error,
                 std::vector<std::string>& usage_errors) {
    AppState state;
    state.load_settings();
    state.vault_registry = load_registry();
    state.needs_vault_pick = false;
    platform::set_active_vault_id("");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // The shell keeps Facet's widget state in data_dir()/ui_state.ini, here the throwaway
    // capture folder. Each scene starts without one, so no scene sees another's state.
    {
        std::error_code ini_ec;
        std::filesystem::remove(platform::data_dir() / L"ui_state.ini", ini_ec);
    }
    ui::configure_ini_file();
    ui::reset_shell_state();
    io.ConfigErrorRecoveryEnableAssert = false;
    io.ConfigErrorRecoveryEnableTooltip = false;
    ImGui::GetCurrentContext()->ErrorCallback = &on_imgui_usage_error;
    ImGui::GetCurrentContext()->ErrorCallbackUserData = &usage_errors;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigInputTextCursorBlink = false;
    io.DisplaySize = ImVec2(static_cast<float>(opt.width), static_cast<float>(opt.height));
    io.DisplayFramebufferScale = ImVec2(1.0F, 1.0F);
    io.DeltaTime = kTimeStep;
    io.AddFocusEvent(true);
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    facet::SetFollowSystemMotion(false);
    facet::SetFollowSystemContrast(false);
    {
        facet::Theme theme = facet::T();
        theme.motion.reduceMotion = true;
        facet::SetTheme(theme, false);
    }
    facet::SetScaleMode(facet::ScaleMode::FollowDpi);
    facet::SetDpiScale(opt.scale);
    apply_capture_look(state.settings, dark);

    bool ok = true;
    if (!ImGui_ImplDX11_Init(gpu.device.Get(), gpu.context.Get())) {
        error = "ImGui_ImplDX11_Init failed";
        ok = false;
    }
    facet::InitDesc desc;
    desc.device = gpu.device.Get();
    desc.context = gpu.context.Get();
    if (ok && !facet::Init(desc)) {
        error = "facet::Init failed";
        ok = false;
    }
    if (ok) {
        ui::fonts::attach_fallbacks();
        SAM_LOG_INFO("ui-capture: {} ({}), fonts: {}", scene.id, dark ? "dark" : "light",
                     facet::FontOriginName(facet::ActiveFontOrigin()));
        // Every scene starts without the dialogs, drawers and popovers an earlier scene opened.
        ui::kit::reset_transient_ui(state);
        scene.setup(state);
        apply_capture_look(state.settings, dark);
        const int frames = std::max(2, scene.settle_frames);
        for (int f = 0; f < frames; ++f) render_frame(gpu, state);
        if (!read_pixels(gpu, opt, pixels)) {
            error = "reading the render target back failed";
            ok = false;
        }
    }

    facet::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    gpu.context->ClearState();
    gpu.context->Flush();
    state.flush_pending_save();
    return ok;
}

}  // namespace

bool register_scene(const CaptureScene& scene) {
    registered_scenes().push_back(scene);
    return true;
}

int run(const wchar_t* cmd_line) {
    (void)cmd_line;
    bind_console();
    make_failures_non_interactive();

    Options opt;
    std::string error;
    if (!parse_options(opt, error)) return fail(error);

    const std::vector<CaptureScene> scenes = all_scenes();
    std::vector<CaptureScene> selected;
    for (const CaptureScene& s : scenes) {
        if (opt.scene.empty() || opt.scene == s.id) selected.push_back(s);
    }
    if (selected.empty()) return fail("no scene with id '" + opt.scene + "'");

    std::error_code ec;
    std::filesystem::create_directories(opt.out_dir, ec);
    if (ec) return fail("cannot create " + path_utf8(opt.out_dir) + ": " + ec.message());
    const std::filesystem::path data_dir = opt.out_dir / L"capture-data";
    std::filesystem::remove_all(data_dir, ec);
    if (std::filesystem::exists(data_dir)) return fail("cannot clear " + path_utf8(data_dir));
    std::filesystem::create_directories(data_dir, ec);
    if (!platform::set_data_dir_override(data_dir)) return fail("the data folder was resolved too early");

    sam::log::init(platform::log_dir());
    SAM_LOG_INFO("ui-capture: data dir {}", path_utf8(data_dir));

    http::cancel_all();
    time_aligner::use_fixed_clock(kDemoClockUnix);
    job_pump::start_workers(2);

    int exit_code = 0;
    if (!prepare_demo_data(error)) {
        exit_code = fail("seeding the demo vaults failed: " + error);
    }

    Gpu gpu;
    if (exit_code == 0 && !create_gpu(gpu, opt, error)) exit_code = fail(error);

    if (exit_code == 0) {
        ui::icons::load(gpu.device.Get());
        ui::widgets::avatar_cache::init(gpu.device.Get());
        ui::widgets::rank_image::init(gpu.device.Get());

        std::vector<unsigned char> pixels;
        int written = 0;
        for (const CaptureScene& scene : selected) {
            for (const bool dark : {false, true}) {
                if ((dark && !opt.dark) || (!dark && !opt.light)) continue;
                const std::string name = std::string(scene.id) + (dark ? "-dark" : "-light") + ".png";
                const std::filesystem::path file = opt.out_dir / std::filesystem::path(name);
                std::string scene_error;
                std::vector<std::string> usage_errors;
                const bool captured = capture_one(gpu, opt, scene, dark, pixels, scene_error, usage_errors);
                // The picture is still written below: these are bugs in the UI code of this
                // scene, and seeing the frame helps to find them.
                for (const std::string& usage_error : usage_errors) {
                    exit_code = fail(name + ": Dear ImGui usage error " + usage_error);
                }
                if (!captured) {
                    exit_code = fail(name + ": " + scene_error);
                    continue;
                }
                const std::string file_utf8 = path_utf8(file);
                if (stbi_write_png(file_utf8.c_str(), opt.width, opt.height, 4, pixels.data(), opt.width * 4) ==
                    0) {
                    exit_code = fail("cannot write " + file_utf8);
                    continue;
                }
                ++written;
                std::printf("wrote %s\n", file_utf8.c_str());
                std::fflush(stdout);
                SAM_LOG_INFO("ui-capture: wrote {}", file_utf8);
            }
        }
        std::printf("ui-capture: %d file(s) written, %s\n", written, exit_code == 0 ? "ok" : "FAILED");
        std::fflush(stdout);

        ui::widgets::rank_image::shutdown();
        ui::widgets::avatar_cache::shutdown();
        ui::icons::shutdown();
    }

    job_pump::stop_workers();
    sam::log::shutdown();
    return exit_code;
}

}  // namespace sam::app::ui_capture

#endif  // SAM_UI_CAPTURE
