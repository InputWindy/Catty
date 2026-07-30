# {{PROJECT_NAME}} plugins

Game-specific optional plugins go here (`*.cplugin` + `Source/<Name>/`).
Built-in engine modules (Platform / Render / GC / Resource) live in `Catty.dll`,
not under `Catty/Plugins/`.

Enabled plugins are scanned when you double-click the `.cproject` (generateProject):
RegisterExtension calls are injected into `Source/Generated/{{PROJECT_NAME}}App.cpp`.
Plugin Public headers are added to the game include path via `Catty::Modules`.

See engine `Catty/Plugins/README.md` for the `.cplugin` schema (`Extension` metadata).
