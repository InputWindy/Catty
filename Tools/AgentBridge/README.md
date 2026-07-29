# Catty Agent Bridge

Local HTTP sidecar that connects the Catty editor **Agent** panel to a Cursor SDK local agent.

## Setup

Requires **Node.js 18+** (22.13+ preferred). On older Node the bridge uses `JsonlLocalAgentStore` instead of sqlite.

```bat
cd Tools\AgentBridge
npm install --registry https://registry.npmjs.org/
```

Set a Cursor API key (Dashboard → API Keys):

```bat
setx CURSOR_API_KEY "your_key_here"
```

Restart the shell / IDE after `setx`. Without a key the bridge runs in **mock** mode so the UI still works.

## Manual run

```bat
node server.mjs --port 8765 --cwd C:\path\to\MyGame
```

The engine normally spawns this automatically when the editor Agent panel attaches.
