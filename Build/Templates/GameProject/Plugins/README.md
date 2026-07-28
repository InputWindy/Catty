# {{PROJECT_NAME}} plugins

Game-specific optional plugins go here (`*.cplugin` + `Source/<CName>/`).
Built-in engine modules (Platform / Render / GC / Resource) live in `Catty.dll`,
not under `Catty/Plugins/`.

Scan roots:

```text
Tools\scan_plugins.bat --cproject {{PROJECT_NAME}}.cproject
```

See engine `Catty/Plugins/README.md` for the `.cplugin` schema.
