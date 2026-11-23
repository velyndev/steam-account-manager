# Vendored dependencies

This directory holds the third-party libraries the project depends on. Most of them
are fetched by `scripts/init_third_party.ps1` and are not committed (see `.gitignore`).
Two subdirectories are tracked in the repository: `protobuf/` and `steam_protos/`.

## Fetched by the init script

| Subdir            | Source                                       | Version  | Why                              |
|-------------------|----------------------------------------------|----------|----------------------------------|
| `imgui/`          | github.com/ocornut/imgui (docking branch)    | v1.93.0-docking | UI                        |
| `facet/`          | github.com/velyndev/imgui-facet              | commit bbbf05b | Design framework on top of ImGui |
| `freetype/`       | github.com/freetype/freetype                 | VER-2-15-0 | Font rasterizer (Segoe UI Variable instances) |
| `protobuf-src/`   | protocolbuffers/protobuf release zip         | v3.21.12 | Sources for the Debug libprotobufd.lib |
| `mbedtls/`        | github.com/Mbed-TLS/mbedtls                  | v3.6.2   | AES-GCM, AES-CBC, PBKDF2, HMAC, RSA |
| `spdlog/`         | github.com/gabime/spdlog                     | v1.14.1  | Logging                          |
| `doctest/`        | github.com/doctest/doctest                   | v2.4.11  | On the include path; no test target in the solution |
| `utfcpp/`         | github.com/nemtrif/utfcpp                    | v4.0.5   | On the include path; no source includes it |
| `nlohmann_json/`  | github.com/nlohmann/json release asset       | v3.11.3  | JSON + CBOR                      |
| `stb/`            | github.com/nothings/stb (`stb_image.h`, `stb_image_write.h`) | master | Image decode for avatars and icons; PNG writing in the screenshot harness |
| `puff/`           | github.com/madler/zlib `contrib/puff`        | v1.3.1   | Inflates gzip-compressed Steam CM messages |
| `minhook/`        | github.com/TsudaKageyu/minhook               | v1.3.3   | API hooks in the HWID spoofer DLL |
| `xorstr/`         | github.com/JustasMasiulis/xorstr (`xorstr.hpp`) | master | String obfuscation in the HWID spoofer DLL |

`stb` and `xorstr` are downloaded from their `master` branch, not from a tag. Every other
entry is pinned to the tag, release or commit in the table.

## Tracked in the repository

| Subdir            | Contents                                     | Why                              |
|-------------------|----------------------------------------------|----------------------------------|
| `protobuf/`       | protobuf v3.21.12: `protoc.exe`, headers, the Release `/MT` libraries | Codegen for the Steam and CS2 messages, Release link |
| `steam_protos/`   | `.proto` files from SteamDatabase            | Input of `scripts/gen_protos.ps1` |

`scripts/gen_protos.ps1` holds the list of `.proto` files that are compiled; it is the
source of truth, not the directory listing.

## Built during the solution build

`facet-build/<toolset>/` is not fetched: `scripts/build_facet.ps1` creates it when the
solution builds (the vcxproj runs it before compiling). It configures `cmake/facet`, which
builds Facet against the exe's own Dear ImGui configuration, plus lunasvg and plutovg (which
Facet's CMake downloads on the first configure), FreeType, and in Debug a `/MTd` libprotobuf
from `protobuf-src/`. The libraries end up in `facet-build/<toolset>/lib/<Configuration>/`.
Deleting the folder forces a clean Facet build.

## Updating a dependency

1. Change the tag (or commit) in `scripts/init_third_party.ps1`.
2. `Remove-Item -Recurse -Force third_party/<name>`.
3. `./scripts/init_third_party.ps1`.
4. Build the solution. Fix any compile errors.

For Facet, also delete `third_party/facet-build/` so the next build configures it again.

## License notes

The dependencies are under permissive licenses: MIT (Dear ImGui, Facet, lunasvg and
plutovg, spdlog, nlohmann/json, doctest), MIT or public domain (stb), Apache-2.0 (xorstr,
and mbedtls, which is dual Apache-2.0 or GPL-2.0-or-later), BSD-3-Clause (protobuf),
BSD-2-Clause (MinHook), BSL-1.0 (utfcpp), the zlib-style license of puff, and the FreeType
License. Each license text or notice comes with the fetched source: a license file in the
library's folder, or the header of a single-file library. lunasvg's is in
`facet-build/<toolset>/_deps/facet_lunasvg-src/`.

The notices these licenses ask for are reproduced in `THIRD_PARTY_NOTICES.md` at the
repository root, for every library whose code ends up in the binaries. The release
workflow puts that file into the release zip, and Settings > About links to it. When a
dependency is added, removed or updated to a version with a different license text,
update that file in the same change.
