# ThirdParty

Vendored dependencies used when network FetchContent is unavailable.

| Tree | Version | Notes |
|------|---------|--------|
| `imgui/` | v1.91.9 | Dear ImGui (MIT). Preferred by `CattyDependencies.cmake`. |
| `nlohmann/json.hpp` | v3.11.3 | nlohmann/json single header (MIT). Used privately by `FJsonDocument`. |

If a vendored tree is removed, CMake falls back to FetchContent from GitHub.
