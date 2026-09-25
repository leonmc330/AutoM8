# Third-party code

Vendored so the project builds with no extra downloads. Both are MIT licensed.

| Library | Version | Files kept |
|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | 1.91.9 | core, `backends/imgui_impl_sdl2.*`, `backends/imgui_impl_sdlrenderer2.*`, `misc/cpp/imgui_stdlib.*` |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | single header `json.hpp` |

Only the files the build uses are kept (no demos, docs or other backends).

SDL2 (2.32.10) and PCRE2 (10.47) are not in this folder: on Linux the system packages are
used, and for Windows / release builds CMake downloads them (pinned by SHA-256, see
`CMakeLists.txt`). Both are under permissive licenses (zlib / BSD).
