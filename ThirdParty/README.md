# ThirdParty

Vendored dependencies used when network FetchContent is unavailable.

| Tree | Version | Notes |
|------|---------|--------|
| `imgui/` | v1.91.9 | Dear ImGui (MIT). Preferred by `CattyDependencies.cmake`. |
| `nlohmann/json.hpp` | v3.11.3 | nlohmann/json single header (MIT). Used privately by `FJsonDocument`. |
| `lua/` | 5.4.x | Optional vendored Lua `src/` (lua.h). Else FetchContent lua-5.4.7. |
| `sol2/` | v3.3.1 | Optional vendored sol2 (`include/sol/sol.hpp`). Else FetchContent. |

If a vendored tree is removed, CMake falls back to FetchContent from GitHub.
