import { randomUUID } from "node:crypto";
import { AgentError } from "../protocol/errors.mjs";

export class UndoJournal {
  constructor() {
    this.records = new Map();
    this.latest_by_world = new Map();
  }

  captureState() {
    return structuredClone({
      records: this.records,
      latest_by_world: this.latest_by_world,
    });
  }

  restoreState(state) {
    const restored = structuredClone(state);
    if (
      !(restored.records instanceof Map) ||
      !(restored.latest_by_world instanceof Map)
    ) {
      throw new AgentError(
        "INTERNAL_ERROR",
        "UndoJournal snapshot did not preserve Map instances"
      );
    }
    this.records = restored.records;
    this.latest_by_world = restored.latest_by_world;
  }

  createRecord({
    world_id,
    request_id,
    before_state,
    changes,
    before_revision,
    after_revision,
  }) {
    const undo_token = randomUUID();
    const record = {
      undo_token,
      world_id,
      request_id,
      before_state: structuredClone(before_state),
      changes: structuredClone(changes),
      before_revision,
      after_revision,
      used: false,
      created_at_ms: Date.now(),
    };
    this.records.set(undo_token, record);
    this.latest_by_world.set(world_id, undo_token);
    return undo_token;
  }

  undo(world, requested_token) {
    const latest_token = this.latest_by_world.get(world.world_id);
    const undo_token = requested_token || latest_token;
    const record = undo_token ? this.records.get(undo_token) : undefined;

    if (
      !record ||
      record.world_id !== world.world_id ||
      record.used ||
      undo_token !== latest_token
    ) {
      throw new AgentError(
        "UNDO_NOT_AVAILABLE",
        "Only the latest successful write transaction can be undone",
        {
          requested_undo_token: requested_token || null,
          latest_undo_token: latest_token || null,
        }
      );
    }

    const current_snapshot = world.snapshot();
    const current_revision = world.revision;
    world.restoreState(record.before_state, { revision: current_revision });
    record.used = true;
    this.latest_by_world.delete(world.world_id);

    const restored_snapshot = world.snapshot();
    return {
      data: {
        undone_token: undo_token,
      },
      changes: [
        {
          operation: "undo_transaction",
          undo_token,
          before: current_snapshot,
          after: restored_snapshot,
        },
      ],
    };
  }
}

