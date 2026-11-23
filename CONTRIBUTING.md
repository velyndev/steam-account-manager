# Contributing

Pull requests are welcome. This is a native Windows C++ app, so the toolchain is
Visual Studio rather than a cross-platform build. This page takes you from a fresh clone
to a built exe and a first UI change.

## Prerequisites

- **Visual Studio 2022** with the **Desktop development with C++** workload. The project
  files pin platform toolset `v143`, and CI (`.github/workflows/build.yml`) builds on
  `windows-2022`.
- **Visual Studio 2026** works too: build with `/p:PlatformToolset=v145` (see below).
- A Windows 10 or 11 SDK (installed with that workload).
- **CMake 3.28 or newer** for the Facet build. `scripts\build_facet.ps1` uses the `cmake`
  on PATH, or else the copy Visual Studio installs with its C++ CMake tools component.
- **Git**, because `scripts\init_third_party.ps1` clones most dependencies, and network
  access for that first fetch and for the first Facet build of each toolset.

## Build

The vendored dependencies under `third_party/` are fetched by a script, so there is
no vcpkg, Conan, or submodule setup.

```powershell
git clone https://github.com/velyndev/steam-account-manager.git
cd steam-account-manager
.\scripts\init_third_party.ps1
start steam-account-manager.sln
```

Pick **`Release | x64`** and build (F7). The binary lands at
`build\Release\steam-account-manager.exe`. Before anything compiles, the project runs
`scripts\gen_protos.ps1`, which regenerates the protobuf sources with the tracked
`protoc`, and `scripts\build_facet.ps1`, which builds the Facet UI framework and its
libraries with CMake. The first Facet build of a toolset downloads lunasvg.

From the command line, `msbuild steam-account-manager.sln /m /p:Configuration=Release /p:Platform=x64`
builds everything. With only the Visual Studio 2026 toolset installed, add
`/p:PlatformToolset=v145`.

The solution also holds `hwid_dll\hwid_spoofer.vcxproj`, which builds
`hwid_spoofer.dll`. When that DLL exists in `build\<Configuration>\`,
`scripts\embed_dll.ps1` embeds it into the exe as `core\hwid\gen\hwid_dll_bytes.hpp`.

Facet is not part of the solution. `cmake\facet\CMakeLists.txt` wraps it, and the
project's `BuildFacet` target runs `build_facet.ps1` before anything compiles and
links the libraries it leaves in `third_party\facet-build\<toolset>\lib\<configuration>\`.
It only rebuilds when a Facet source, header or icon changes. Run the script by hand
(`.\scripts\build_facet.ps1 -Configuration Debug -Toolset v145`) only to look at a
CMake error on its own.

Debug builds also work: `build_facet.ps1` builds a `/MTd` libprotobuf from the
fetched protobuf sources for them. For UI work, build **`Debug | x64`**: only Debug
contains the screenshot harness described below.

## Your first UI change

### Where things live

| What you see | Code |
|---|---|
| Title bar, sidebar, command palette, activity item, update dialog, toasts | `ui\shell\` (`title_bar.cpp`, `sidebar.cpp`, `command_palette.cpp`, `activity.cpp`, `overlays.cpp`), framed by `ui\main_window.cpp` |
| Accounts | `ui\screens\accounts_screen.cpp` and `ui\screens\accounts\` |
| Authenticator | `ui\screens\sda_screen.cpp` and `ui\screens\authenticator\` |
| Confirmations | `ui\screens\confirmations_screen.cpp` (fetch and respond), `ui\screens\confirmations\` (view and dialogs) |
| Trade offers | `ui\screens\trade_offers_screen.cpp`, `trade_offers_modals.cpp` and `ui\screens\trade_offers\` |
| CS2 | `ui\screens\cs2_screen.cpp` and `ui\screens\cs2\` |
| Add account | `ui\screens\add_account_screen.cpp`, the `add_account_*.cpp` files beside it, and `ui\screens\add_account\`; the bundle dialogs in `ui\widgets\bundle_dialogs.cpp` |
| Settings | `ui\screens\settings_screen.cpp` (category list, search, save bar) and one file per category in `ui\screens\settings\` |
| Unlock and vault picker | `ui\screens\unlock_screen.cpp`, `vault_picker_screen.cpp` and `ui\screens\unlock\` |

A new Settings option is drawn with the row helpers of `ui\screens\settings\page.hpp`
(`toggle_row`, `number_row`, `begin_row`). An option that waits for Save also gets a
`SAM_PENDING` line in `ui\screens\settings\page.cpp`; one that acts at once calls
`commit_now`. The value itself is a field of `app\settings.hpp`, written and read in
`app\app_state.cpp`.

### Adding a file

`steam-account-manager.vcxproj` and `steam-account-manager.vcxproj.filters` list
every source file explicitly (no globbing). When you add, remove, or rename a file
you must update **both**:

- the `.vcxproj`: `<ClCompile Include="ui\screens\settings\my_file.cpp" />` for a
  `.cpp`, `<ClInclude ... />` for a header;
- the `.vcxproj.filters`: the same entry with a `<Filter>` naming its folder, for example
  `<ClCompile Include="ui\screens\settings\my_file.cpp"><Filter>ui\screens\settings</Filter></ClCompile>`.

### Looking at it: the screenshot harness

Debug defines `SAM_UI_CAPTURE`, which compiles a screenshot harness into the exe.
Use it to look at the UI while you work on it: a normal launch asks for administrator
and opens your real vault, while the harness renders the real UI offscreen with demo
data (30 fictional accounts, pending confirmations and trade offers, a fixed clock).
Its data folder is `<outdir>\capture-data`, wiped at the start; every HTTP request
fails at once, and it never elevates, opens a window, or touches Steam.

```powershell
build\Debug\steam-account-manager.exe --ui-capture <outdir> [--scene <id>] [--theme light|dark|both] [--size 1280x800] [--scale 1.0] [--gpu]
```

It writes `<outdir>\<scene>-<theme>.png` for every registered scene, or only the
one given with `--scene`, and exits with 0. `--theme` defaults to both, `--size`
(64 to 8192 per side) to 1280x800, and `--scale`, the display scale from 0.5 to 4, to
1.0. The WARP software rasterizer draws unless `--gpu` is given, so the pixels do not
depend on the graphics driver. Dear ImGui usage errors (an unbalanced `Begin`/`End`,
`SetCursorPos` past the end of a window) are reported on stderr with the window they
happened in, and the run exits non-zero; the picture is still written. A build without
`SAM_UI_CAPTURE` (Release) refuses `--ui-capture`.

To add a scene, register a setup function with `SAM_UI_CAPTURE_SCENE` inside
`#ifdef SAM_UI_CAPTURE`; the header comment of `app\ui_capture.hpp` shows how. The
shell's scenes are in `app\ui_capture_scenes.cpp` and `ui\shell\capture_scenes.cpp`;
screens keep theirs in their own folder (`ui\screens\<screen>\capture_scenes.cpp` or
`*_capture.cpp`). Run the harness with `--scene <id>` for the scene you touched, in both
themes.

In the normal app, Dear ImGui usage errors never open a dialog: each one is written
to the log once.

## Working with Facet

The UI is built on [Facet](https://github.com/velyndev/imgui-facet), a design framework
on top of Dear ImGui. The code follows these conventions:

- **Sizes are design units.** Facet widget options (`.width = 260.0F`) take units, and
  `facet::Px(units)` converts units to pixels at the current scale (monitor DPI times the
  interface size setting). Plain Dear ImGui calls take pixels, so units are wrapped there:
  `ImGui::Dummy(ImVec2(0.0F, facet::Px(facet::T().spacing.sm)))`.
- **Colors, spacing and metrics come from the theme**, through `facet::T()`
  (`facet::T().spacing.sm`, `facet::T().colors.separator`), not from literals.
  `ui\theme.cpp` pushes the appearance settings (theme, accent, interface size) into Facet.
- **Shared building blocks live in `ui\kit\`**, on top of Facet: the page and its header
  (`kit::PageScope`, `kit::PageHeaderScope`, used by every screen), toasts
  (`kit::show_toast`), confirm and destructive dialogs, empty states, account rows and
  pickers, standing marks (bans, cooldown, trust), the authenticator code, navigation
  helpers and formatting. Use them before writing a new widget in a screen.
- **Transient UI never outlives its context.** A screen registers the function that
  closes its dialogs, drawers and popovers with `SAM_KIT_TRANSIENT_RESET` (runs on lock,
  on a vault change, and before every capture scene) or `SAM_KIT_SCREEN_LEAVE` (runs when
  the user leaves the screen); see `ui\kit\transient.hpp`.
- **Keyboard shortcuts** are registered with `facet::RegisterShortcut`; the shortcut sheet
  (Ctrl+/) lists every registered one. The app's global shortcuts are in
  `ui\shell\shell.cpp`.
- **Facet is not patched in this repo.** `third_party\facet` is a clean clone of the commit
  pinned in `scripts\init_third_party.ps1`. A change Facet needs goes to the Facet
  repository, and the pin moves here afterwards.
- `cmake\facet\CMakeLists.txt` compiles Facet with the same Dear ImGui definitions as
  `steam-account-manager.vcxproj` (`IMGUI_USE_WCHAR32`, `IMGUI_ENABLE_FREETYPE` and the
  rest). Change them in both places or in neither.

## Style and quality

- Formatting is governed by `.clang-format` and `.editorconfig` at the repo root.
  Run clang-format before committing.
- The project builds at `/W4`. Warnings are not treated as errors, so keep your
  changes free of new ones. CI (`.github/workflows/build.yml`) builds the same Release x64
  config on pushes and pull requests to `main`.
- Interfaces are documented in headers, where a function or type is declared, and that
  documentation states what the code does. In a `.cpp` file a comment is allowed only
  where it states a reason the code cannot show (why a layout is ordered the way it is,
  why a value is what it is), apart from the `}  // namespace ...` markers that close a
  namespace. The `.cpp` files under `core/`, `platform/` and `hwid_dll/` have no other
  comments. No comment narrates history: what changed belongs in the commit message.

## Layout

```
app/            WinMain (window, drag and drop), app state and settings, vault registry,
                background vault writer, job pump, confirmation poller, cleaner runner,
                CS2 auto-pull, the gamesense and luminary loaders, and the Debug-only
                screenshot harness (ui_capture*)
core/
  account_store/  vault types, atomic-write format, filter / sort, groups / tags
  cleaner/        tracer cleaner: Steam scan, targets, profiles, plan and execution
  crypto/         AES-GCM, AES-CBC, HMAC, PBKDF2, Rijndael-256, RNG, SecureString
  sda/            TOTP, confirmations, maFile / info.dat import and export, add / remove
                  Guard, confirmation history
  steam_login/    mobile auth flow (RSA, BeginAuthSession, poll, finalizelogin),
                  web session, browser login
  steam_cm/       Steam CM connection, CM server list, web-token minting
  cs2_gc/         CS2 Game Coordinator client, item schema, storage units, weekly drop
  steam_api/      Steam Web API (summaries, bans, level, owned games)
  steam_gcpd/     GCPD scraper for CS2 Premier / Wingman / cooldown / VAC-Live
  steam_local/    loginusers.vdf parsing, connect-cache token injection, login prefs
  steam_spend/    external funds (TotalSpend) scrape
  steam_auth/     generated protobuf code
  trade/          trade offers, inventory, actions, trade-URL parsing, trade history
  launch/         steam.exe relaunch, UI-Automation login driver, token launcher,
                  CS2 autostart, clipboard auto-clear
  hwid/           hardware profiles and the injector for the spoofer DLL
  cs2/            CS2 friend-code conversion
  cs2_config/     CS2 video.txt / 730 / userdata deploy, launch options, workshop block
  profile/        Steam display-name change
  notifications/  ban / cooldown change store
  http/           WinHTTP client and proxy (in-process SOCKS5 bridge)
platform/       Win32 wrappers: paths, registry, process, clipboard, DPAPI, tray
                icon, startup task, UI Automation, global hotkey, file dialog, DPI,
                window capture exclusion, single instance, browser launch
ui/
  main_window     the frame: title bar, sidebar and the current screen
  theme, fonts,   bridge from the appearance settings to Facet, fallback fonts,
  icons           the app's own glyphs
  shell/          title bar, sidebar with the vault switcher, command palette,
                  shortcut sheet, activity item, update dialog, toasts
  kit/            shared building blocks on Facet: page and header, navigation,
                  account rows and pickers, standing marks (bans, cooldown, trust),
                  authenticator code, dialogs and toasts, table rows, formatting,
                  transient-state resets
  screens/        one entry file per screen (*_screen.cpp: unlock, vault picker,
                  accounts, authenticator (sda), confirmations, trade offers, CS2,
                  add account, settings), some part files beside them, and a folder
                  per screen with its parts and capture scenes
  widgets/        image and avatar cache, rank images, the toast queue, bundle
                  export and import
hwid_dll/       the HWID spoofer DLL (MinHook hooks), built as its own project
third_party/    fetched by scripts\init_third_party.ps1 (protobuf/ and steam_protos/
                are tracked)
assets/         app icon and CS2 rank images, compiled into the .exe via app.rc
cmake/facet/    CMake wrapper that builds Facet and its libraries
scripts/        third-party fetch, protobuf codegen, Facet build, DLL embedding
```

The `core/` and `ui/` split keeps everything in `core/` headless.

## Vendored libraries

| Library | License | Used for |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) (docking) | MIT | UI |
| [Facet](https://github.com/velyndev/imgui-facet) | MIT | Design framework: theme, fonts, widgets |
| [lunasvg](https://github.com/sammycage/lunasvg) and plutovg | MIT | SVG icons (through Facet) |
| [FreeType](https://freetype.org) | FreeType License | Font rasterization |
| [mbedtls](https://github.com/Mbed-TLS/mbedtls) | Apache-2.0 (or GPL-2.0-or-later) | AES-GCM, AES-CBC, PBKDF2, HMAC, RSA |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT | JSON, CBOR |
| [spdlog](https://github.com/gabime/spdlog) | MIT | Logging |
| [stb](https://github.com/nothings/stb) | MIT / public domain | PNG decode for avatars, PNG writing in the harness |
| [puff](https://github.com/madler/zlib/tree/master/contrib/puff) | zlib-style | Inflating compressed Steam CM messages |
| [MinHook](https://github.com/TsudaKageyu/minhook) | BSD-2-Clause | API hooks in the HWID spoofer DLL |
| [xorstr](https://github.com/JustasMasiulis/xorstr) | Apache-2.0 | String obfuscation in the HWID spoofer DLL |
| [protobuf](https://github.com/protocolbuffers/protobuf) | BSD-3-Clause | Codegen for the Steam and CS2 messages |
| [utfcpp](https://github.com/nemtrif/utfcpp) | BSL-1.0 | Fetched; no source includes it |
| [doctest](https://github.com/doctest/doctest) | MIT | Fetched; no test target in the solution |

Versions and the fetch details are in [third_party/README.md](third_party/README.md).
The license notices of the libraries that end up in the binaries are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); a change that adds a library adds its
notice there.
