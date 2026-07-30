# Agent Core v0.2 behavior evaluations

`npm run eval` runs deterministic, network-free behavior scenarios against
`MockProvider`, `AgentService`, `CommandExecutor`, and an independent in-memory
Session and `MockWorld` for every scenario.

Case data lives in `evals/cases/*.json`; execution and assertions live in
`eval-runner.mjs` and `assertions.mjs`. A case may define `initial_entities`,
ordered `turns`, per-turn expectations, and final world expectations.

Supported per-turn checks include assistant text, tool names and count, exact
or relative revision, entity count, partial entity transform/properties,
no-world-change, undo creation, and clarification. Failure output identifies
the scenario, turn, expected value, and actual value. Any failure exits
nonzero; success prints passed/failed counts and elapsed time.

## Case format

```json
{
  "suite": "example",
  "cases": [
    {
      "name": "move a recent entity",
      "initial_entities": [],
      "turns": [
        {
          "message": "生成一个立方体",
          "expect": {
            "tool_names": ["entity.spawn_primitive"],
            "tool_call_count": 1,
            "revision": 1,
            "entity_count": 1,
            "undo_created": true
          }
        },
        {
          "message": "把它移动到 3, 1, 5",
          "expect": {
            "tool_names": ["entity.set_transform"],
            "revision_delta": 1,
            "entity": {
              "name": "cube_1",
              "transform": {
                "position": [3, 1, 5]
              }
            }
          }
        }
      ],
      "final": {
        "revision": 2,
        "entity_count": 1
      }
    }
  ]
}
```

`initial_entities` are fixture state and begin at revision 0 with no Session
reference. Entity expectations are partial matches selected by `entity_id`,
`name`, or `primitive_type`.

Set `clarifies: true` to require an empty ToolCall list, clear clarification
text, an unchanged world snapshot, and no undo token. Set
`no_world_change: true` for other refusal or read-only cases.

## Current suites

- `basic-commands.json`: create, list, query, delete, undo, primitive/color and
  Chinese/English wording variants.
- `multi-turn-references.json`: recent creation/query references and lifecycle.
- `relative-updates.json`: relative movement/scale, color, visibility, and
  one-call compound creation.
- `ambiguity.json`: no referent, duplicate name/type, and multiple-entity
  clarification.
- `safety.json`: excessive creation, command/file/script requests, extreme
  coordinates, and illegal scale.

The evaluations intentionally cover MockWorld and MockProvider only. They do
not contact Cursor, use an API key, access an external network, or integrate
with the C++ game World. MockProvider is deterministic test scaffolding rather
than a general natural-language model.
