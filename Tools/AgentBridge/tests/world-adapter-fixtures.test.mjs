import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  validateAdapterCapabilities,
  validateAdapterCapabilityRequest,
  validateExecuteTransactionInput,
  validateExecuteTransactionResult,
  validateHealthResult,
  validateUndoResult,
  validateWorldSnapshot,
} from "../src/world/world-adapter-contract.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";

const FIXTURE_DIRECTORY = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "fixtures",
  "world-adapter-v1"
);

const EXPECTED_FIXTURES = [
  "execute-dry-run-unsupported-error.json",
  "execute-spawn-request.json",
  "execute-spawn-success.json",
  "execute-too-many-tools-error.json",
  "execute-transform-request.json",
  "execute-transform-success.json",
  "execute-unsupported-tool-error.json",
  "health-full.json",
  "health-invalid-undo-dependency.json",
  "health-minimal.json",
  "health-unknown-tool.json",
  "revision-conflict-error.json",
  "snapshot-empty.json",
  "snapshot-one-entity.json",
  "undo-unsupported-error.json",
];

const INVALID_HEALTH_FIXTURES = new Set([
  "health-invalid-undo-dependency.json",
  "health-unknown-tool.json",
]);

async function fixture(name) {
  return JSON.parse(
    await fs.readFile(path.join(FIXTURE_DIRECTORY, name), "utf8")
  );
}

function withoutVersion(envelope) {
  const dto = structuredClone(envelope);
  delete dto.adapter_protocol_version;
  return dto;
}

function knownToolNames(registry) {
  return registry.listDefinitions().map((definition) => definition.name);
}

function validateToolArguments(registry, request) {
  for (const tool_call of request.tool_calls) {
    registry.validate(tool_call.tool_name, tool_call.args);
  }
}

function errorRequest(envelope, tool_calls, overrides = {}) {
  return {
    request_id: envelope.request_id,
    session_id: envelope.session_id,
    world_id: envelope.world_id,
    expected_revision: envelope.before_revision,
    dry_run: false,
    atomic: false,
    tool_calls,
    ...overrides,
  };
}

test("World Adapter v1 golden fixture inventory is complete and deterministic", async () => {
  const names = (await fs.readdir(FIXTURE_DIRECTORY)).sort();
  assert.deepEqual(names, EXPECTED_FIXTURES);
  for (const name of names) {
    const raw = await fs.readFile(path.join(FIXTURE_DIRECTORY, name), "utf8");
    assert.equal(raw.includes("//"), false);
    assert.equal(raw.includes("timestamp_ms\": "), name.startsWith("snapshot-"));
    assert.equal(JSON.parse(raw).adapter_protocol_version, "1.0");
  }
});

test("health fixtures use runtime capability validation and mark invalid profiles", async () => {
  const registry = createDefaultToolRegistry();
  const known_tools = knownToolNames(registry);
  for (const name of EXPECTED_FIXTURES.filter((entry) =>
    entry.startsWith("health-")
  )) {
    const envelope = await fixture(name);
    if (INVALID_HEALTH_FIXTURES.has(name)) {
      assert.throws(
        () => {
          const health = validateHealthResult(envelope);
          validateAdapterCapabilities(health.capabilities, { known_tools });
        },
        (error) =>
          error instanceof WorldAdapterError &&
          error.reason === "capability_insufficient"
      );
    } else {
      const health = validateHealthResult(envelope);
      validateAdapterCapabilities(health.capabilities, { known_tools });
    }
  }
});

test("snapshot fixtures validate through the runtime WorldSnapshot schema", async () => {
  const registry = createDefaultToolRegistry();
  for (const name of ["snapshot-empty.json", "snapshot-one-entity.json"]) {
    const envelope = await fixture(name);
    validateAdapterCapabilities(envelope.capabilities, {
      known_tools: knownToolNames(registry),
    });
    validateWorldSnapshot(
      {
        world_id: envelope.world_id,
        revision: envelope.world_revision,
        entities: envelope.entities,
        history: envelope.history,
      },
      { expected_world_id: envelope.world_id }
    );
  }
});

test("success request and response fixtures validate with current runtime contracts", async () => {
  const registry = createDefaultToolRegistry();
  const capabilities = (await fixture("health-minimal.json")).capabilities;
  for (const stem of ["spawn", "transform"]) {
    const request = withoutVersion(
      await fixture(`execute-${stem}-request.json`)
    );
    const response = withoutVersion(
      await fixture(`execute-${stem}-success.json`)
    );
    validateExecuteTransactionInput(request);
    validateToolArguments(registry, request);
    validateAdapterCapabilityRequest(capabilities, {
      tool_calls: request.tool_calls,
      dry_run: request.dry_run,
      atomic: request.atomic,
      known_tools: knownToolNames(registry),
    });
    validateExecuteTransactionResult(response, request);
  }
});

test("capability and revision error fixtures remain valid response DTOs", async () => {
  const registry = createDefaultToolRegistry();
  const known_tools = knownToolNames(registry);
  const capabilities = (await fixture("health-minimal.json")).capabilities;
  const cases = [
    {
      name: "execute-unsupported-tool-error.json",
      calls: [
        {
          tool_call_id: "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
          tool_name: "entity.destroy",
          args: { entity_id: "55555555-5555-4555-8555-555555555555" },
        },
      ],
      capability_error: true,
    },
    {
      name: "execute-too-many-tools-error.json",
      calls: [
        {
          tool_call_id: "44444444-4444-4444-8444-444444444444",
          tool_name: "world.get_summary",
          args: {},
        },
        {
          tool_call_id: "77777777-7777-4777-8777-777777777777",
          tool_name: "world.get_summary",
          args: {},
        },
      ],
      capability_error: true,
      atomic: true,
    },
    {
      name: "execute-dry-run-unsupported-error.json",
      calls: [
        {
          tool_call_id: "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
          tool_name: "world.get_summary",
          args: {},
        },
      ],
      capability_error: true,
      dry_run: true,
    },
    {
      name: "revision-conflict-error.json",
      calls: [
        {
          tool_call_id: "77777777-7777-4777-8777-777777777777",
          tool_name: "entity.set_transform",
          args: {
            entity_id: "55555555-5555-4555-8555-555555555555",
            transform: { position: [10, 20, 30] },
          },
        },
      ],
      capability_error: false,
      expected_revision: 1,
    },
  ];

  for (const item of cases) {
    const response = withoutVersion(await fixture(item.name));
    const request = errorRequest(response, item.calls, {
      expected_revision: item.expected_revision ?? response.before_revision,
      dry_run: item.dry_run ?? false,
      atomic: item.atomic ?? false,
    });
    validateExecuteTransactionInput(request);
    validateToolArguments(registry, request);
    if (item.capability_error) {
      assert.throws(
        () =>
          validateAdapterCapabilityRequest(capabilities, {
            tool_calls: request.tool_calls,
            dry_run: request.dry_run,
            atomic: request.atomic,
            known_tools,
          }),
        WorldAdapterError
      );
    } else {
      validateAdapterCapabilityRequest(capabilities, {
        tool_calls: request.tool_calls,
        dry_run: request.dry_run,
        atomic: request.atomic,
        known_tools,
      });
    }
    validateExecuteTransactionResult(response, request);
  }
});

test("undo unsupported fixture validates through the runtime undo contract", async () => {
  const response = withoutVersion(await fixture("undo-unsupported-error.json"));
  validateUndoResult(response, {
    request_id: response.request_id,
    session_id: response.session_id,
    world_id: response.world_id,
    expected_revision: response.before_revision,
    undo_token: null,
  });
});
