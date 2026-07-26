# ThirdParty

Vendored dependencies used when network FetchContent is unavailable.

| Tree | Version | Notes |
|------|---------|--------|
| `imgui/` | v1.91.9 | Dear ImGui (MIT). Preferred by `CattyDependencies.cmake`. |

If `imgui/` is removed, CMake falls back to FetchContent from GitHub (`v1.91.9`).
