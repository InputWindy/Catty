# {{PROJECT_NAME}} plugins

Game-specific optional plugins go here (`*.cplugin` + `Source/<Name>/`).
Built-in engine modules (Platform / Render / GC / Resource) live in `Maho.dll`,
not under `Maho/Plugins/`.

Enabled plugins are scanned when you double-click the `.cproject` (generateProject):
RegisterExtension calls are injected into `Source/Generated/{{PROJECT_NAME}}App.cpp`.
Plugin Public headers are added to the game include path via `Maho::Modules`.

See engine `Maho/Plugins/README.md` for the `.cplugin` schema (`Extension` metadata).
