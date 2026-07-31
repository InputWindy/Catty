export const RECENT_ENTITY_LIMIT = 20;

export function createEntityContext() {
  return {
    last_created_entity_id: null,
    last_referenced_entity_id: null,
    last_query_entity_ids: [],
    recent_entity_ids: [],
  };
}

function entityExists(snapshot, entity_id) {
  return Boolean(
    entity_id &&
    snapshot.entities.some((entity) => entity.entity_id === entity_id)
  );
}

function rememberEntity(session, snapshot, entity_id) {
  const context = session.entity_context;
  if (!entityExists(snapshot, entity_id)) {
    return;
  }
  context.last_referenced_entity_id = entity_id;
  context.recent_entity_ids = [
    entity_id,
    ...context.recent_entity_ids.filter((id) => id !== entity_id),
  ].slice(0, RECENT_ENTITY_LIMIT);
}

function forgetEntity(session, entity_id) {
  const context = session.entity_context;
  if (context.last_created_entity_id === entity_id) {
    context.last_created_entity_id = null;
  }
  if (context.last_referenced_entity_id === entity_id) {
    context.last_referenced_entity_id = null;
  }
  context.last_query_entity_ids = context.last_query_entity_ids.filter(
    (id) => id !== entity_id
  );
  context.recent_entity_ids = context.recent_entity_ids.filter(
    (id) => id !== entity_id
  );
}

export function reconcileEntityContext(session, snapshot) {
  const context = session.entity_context;
  if (!entityExists(snapshot, context.last_created_entity_id)) {
    context.last_created_entity_id = null;
  }
  if (!entityExists(snapshot, context.last_referenced_entity_id)) {
    context.last_referenced_entity_id = null;
  }
  context.last_query_entity_ids = context.last_query_entity_ids.filter((id) =>
    entityExists(snapshot, id)
  );
  context.recent_entity_ids = context.recent_entity_ids
    .filter((id, index, ids) => ids.indexOf(id) === index)
    .filter((id) => entityExists(snapshot, id))
    .slice(0, RECENT_ENTITY_LIMIT);
}

export function applyToolResultsToEntityContext({
  session,
  snapshot,
  tool_calls,
  tool_results,
}) {
  if (tool_results.some((result) => result.dry_run)) {
    return;
  }

  for (const [index, tool_call] of tool_calls.entries()) {
    const result = tool_results[index];
    if (!result?.ok) {
      continue;
    }
    if (tool_call.tool_name === "entity.spawn_primitive") {
      const entity_id = result.data?.entity?.entity_id;
      if (entityExists(snapshot, entity_id)) {
        session.entity_context.last_created_entity_id = entity_id;
        rememberEntity(session, snapshot, entity_id);
      }
      continue;
    }
    if (tool_call.tool_name === "entity.get") {
      const entity_id = result.data?.entity?.entity_id;
      session.entity_context.last_query_entity_ids = entityExists(
        snapshot,
        entity_id
      )
        ? [entity_id]
        : [];
      rememberEntity(session, snapshot, entity_id);
      continue;
    }
    if (tool_call.tool_name === "world.query_entities") {
      const entity_ids = (result.data?.entities || [])
        .map((entity) => entity.entity_id)
        .filter((entity_id) => entityExists(snapshot, entity_id));
      session.entity_context.last_query_entity_ids = entity_ids;
      if (entity_ids.length === 1) {
        rememberEntity(session, snapshot, entity_ids[0]);
      }
      continue;
    }
    if (
      tool_call.tool_name === "entity.set_transform" ||
      tool_call.tool_name === "entity.set_property"
    ) {
      rememberEntity(session, snapshot, result.data?.entity?.entity_id);
      continue;
    }
    if (tool_call.tool_name === "entity.destroy") {
      forgetEntity(session, tool_call.args.entity_id);
    }
  }

  reconcileEntityContext(session, snapshot);
}
