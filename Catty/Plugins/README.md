# Catty Engine Plugins

Drop engine-wide plugins here. Each plugin is a folder with a `.cplugin` manifest
(JSON, UTF-8). Game-specific plugins belong in the game project's `Plugins/`.

## `.cplugin` schema (FileVersion 1)

```json
{
  "FileVersion": 1,
  "Version": 1,
  "VersionName": "1.0",
  "FriendlyName": "Catty Runtime",
  "Description": "...",
  "Category": "Engine",
  "EnabledByDefault": true,
  "Modules": [
    {
      "Name": "Platform",
      "Type": "Runtime",
      "Dependencies": ["Engine"]
    }
  ]
}
```

| Field | Required | Notes |
|-------|----------|-------|
| `FileVersion` | yes | Manifest format version (`1`) |
| `Modules` | yes | Non-empty array |
| `Modules[].Name` | yes | Unique across **all** discovered plugins |
| `Modules[].Type` | no | Default `Runtime` (`Runtime` / `Editor` / `Developer`) |
| `Modules[].Dependencies` | no | Other **module** names (not plugin names); empty = no deps |
| `EnabledByDefault` | no | Default `true`; scan skips when `false` unless `--include-disabled` |

## Layout

```text
Catty/Plugins/<PluginName>/<PluginName>.cplugin
Catty/Plugins/<PluginName>/Source/<ModuleName>/   # future per-module sources / DLL
```

The `.cplugin` file name should match the plugin folder name (e.g. `CattyRuntime/CattyRuntime.cplugin`).

## Scan tool

```text
Tools\scan_plugins.bat
Tools\scan_plugins.bat --cproject path\to\Game.cproject --out Intermediate\plugin_modules.json
```

Resolves module dependency topology (startup / shutdown order), detects missing deps and cycles. Output JSON is intended for the CMake multi-DLL build step.
