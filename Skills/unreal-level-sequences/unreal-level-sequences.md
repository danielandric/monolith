---
name: unreal-level-sequences
description: Use when inspecting Unreal Level Sequence Director Blueprints via Monolith MCP — listing sequences with directors, reading director functions and variables, walking event-track bindings (Possessable/Spawnable/master), and reverse-looking-up which sections fire a given director function across the project. Triggers on level sequence, sequencer, director blueprint, event track, FMovieSceneEvent, possessable, spawnable, cinematics.
---

# Unreal Level Sequence Director Workflows

**6 inspection actions** via `level_sequence_query()`. Discover with `monolith_discover({ namespace: "level_sequence" })`.

Indexes only Level Sequences that carry a Director Blueprint. LSes without a Director are silently skipped (correct: such sequences play, but have no Director-driven event logic to inspect). Synthetic `SequenceEvent__ENTRYPOINT<DirBP>_N` UFunctions UE generates for "Quick Bind" event entries are indexed alongside user `Subtitle()`-style functions and `K2Node_CustomEvent` graph events — they are the real runtime targets of event-track entries.

## Key Parameters

- `asset_path` — full Level Sequence object path (e.g. `/Game/Cinematics/LS_Intro.LS_Intro`); matches `ULevelSequence::GetPathName()`
- `asset_path_filter` — optional glob (`*` and `?`) for `list_directors` and `find_director_function_callers`; converted to SQL `LIKE`
- `kind` — function-classification filter for `list_director_functions`: `user` / `custom_event` / `sequencer_endpoint` / `event` (alias for the latter two) / `all`
- `function_name` — exact, case-sensitive name for `find_director_function_callers`

## Action Reference

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `list_directors` | `asset_path_filter`? | All LSes with a Director BP, ordered by path. Each row: `ls_path`, `director_bp_name`, `function_count`, `variable_count` |
| `get_director_info` | `asset_path` | One Director's summary: `function_breakdown` grouped by kind, `variable_count`, `event_bindings.{total,resolved}`, sample of up to 10 functions ordered user → custom_event → sequencer_endpoint |
| `list_director_functions` | `asset_path`, `kind`? | Director's own functions with parsed signatures. Inherited base-class methods and compiler-generated `ExecuteUbergraph*` are not indexed (own-only, matches `blueprint_query` convention) |
| `list_director_variables` | `asset_path` | Director's own variables (name + K2-formatted type) in declaration order |
| `list_event_bindings` | `asset_path` | All event-track entries grouped by binding GUID. Each binding shows kind (`possessable` / `spawnable` / `master`), bound class, and an array of sections (`trigger` / `repeater`) with the Director function each fires (resolved name + kind + signature when matched) |
| `find_director_function_callers` | `function_name`, `asset_path_filter`? | Cross-sequence reverse lookup. Returns every event-track section across the project that fires the named function, with LS path and binding context (binding GUID/name/kind/bound class, section kind, resolved bool) |

## Common Workflows

### "Which sequences in module X have a Director?"
```
level_sequence_query({ action: "list_directors", params: { asset_path_filter: "/Game/Cinematics/*" } })
```

### "What does this LS Director do?"
```
level_sequence_query({ action: "get_director_info", params: { asset_path: "/Game/Cinematics/LS_Intro.LS_Intro" } })
level_sequence_query({ action: "list_director_functions", params: { asset_path: "/Game/Cinematics/LS_Intro.LS_Intro", kind: "user" } })
level_sequence_query({ action: "list_director_variables", params: { asset_path: "/Game/Cinematics/LS_Intro.LS_Intro" } })
```

### "Which actor in the level fires which Director function in this LS?"
```
level_sequence_query({ action: "list_event_bindings", params: { asset_path: "/Game/Cinematics/LS_Intro.LS_Intro" } })
```
Returns bindings grouped by GUID — for each Possessable / Spawnable / master track, see all event-track sections and the Director function each fires.

### "Where else is this Director function called from?"
```
level_sequence_query({ action: "find_director_function_callers", params: { function_name: "OnPlayerEntered" } })
level_sequence_query({ action: "find_director_function_callers", params: { function_name: "SequenceEvent__ENTRYPOINTLS_Intro_DirectorBP_0", asset_path_filter: "/Game/Cinematics/*" } })
```

### Combine with `blueprint_query` for full graph introspection
The Director Blueprint is also a regular `UBlueprint` accessible via `blueprint_query` using `subobject:` syntax:
```
blueprint_query({ action: "get_blueprint_info", params: { asset_path: "/Game/Cinematics/LS_Intro.LS_Intro:LS_Intro_DirectorBP" } })
blueprint_query({ action: "get_graph_summary", params: { asset_path: "...:LS_Intro_DirectorBP", graph_name: "Sequencer Events" } })
```
`level_sequence_query` covers the Sequencer-side metadata (bindings, event-track structure); `blueprint_query` covers the BP graph-level details.

## Rules

- **Read-only namespace.** No write actions in this MVP — purely inspection.
- **Path format strict.** Pass the full object path returned by `ULevelSequence::GetPathName()` (e.g. `/Module/.../File.File`), not just `/Module/.../File`. Errors include a hint.
- **`function_name` matching is exact and case-sensitive.** `Start` will not match `start_event`.
- **Glob conversion.** `*` → `%`, `?` → `_`. Single quotes are escaped automatically.
- **`kind` filter values.** Use `event` as an alias when you want both `custom_event` (graph-defined `K2Node_CustomEvent`) and `sequencer_endpoint` (UE-generated `SequenceEvent__ENTRYPOINT*`). Use the precise values when you need to distinguish.
- **`fires_function_id` may be NULL** even when `fires_function_name` is set — this happens for empty trigger sections (no key-frames yet) and the rare cross-Director call. Check the `resolved` boolean in `find_director_function_callers` output.
- **Function counts include synthetic functions.** `function_count` on a Director includes user functions + graph CustomEvents + Sequencer-generated endpoints. Use `list_director_functions` with `kind` to slice.
- **Inherited base-class API is intentionally not indexed.** `ULevelSequenceDirector::OnCreated`, `GetBoundActor`, `GetCurrentTime`, etc. won't show up — this matches `blueprint_query.get_functions` convention.
