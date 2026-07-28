# {{PROJECT_NAME}} plugins

Game-specific plugins go here (`*.cplugin` + `Source/<ModuleName>/`).
Engine plugins live under the engine's `Catty/Plugins/`.

Scan both roots:

```text
Tools\scan_plugins.bat --cproject {{PROJECT_NAME}}.cproject
```

See engine `Catty/Plugins/README.md` for the `.cplugin` schema.
