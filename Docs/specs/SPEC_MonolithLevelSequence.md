# Monolith — MonolithLevelSequence Module

**Parent:** [SPEC_CORE.md](../SPEC_CORE.md)
**Engine:** Unreal Engine 5.7+
**Version:** 0.14.7 (Beta)

---

## MonolithLevelSequence

**Dependencies:** Core, CoreUObject, Engine, MonolithCore, MonolithIndex, SQLiteCore, UnrealEd, MovieScene, MovieSceneTracks, LevelSequence, BlueprintGraph, Kismet, EditorSubsystem, Json, JsonUtilities
**Namespace:** `level_sequence` | **Tool:** `level_sequence_query(action, params)` | **Actions:** 7 (6 production + `ping` smoke)
**Settings toggles:** `bEnableLevelSequence` (registers actions, default True) and `bIndexLevelSequences` (registers indexer, default True) — both under `[/Script/MonolithCore.MonolithSettings]`

MonolithLevelSequence introspects `ULevelSequence` assets that carry a Director Blueprint, exposing the Director's own functions and variables, and the event-track bindings that fire those functions at runtime. It complements `MonolithBlueprint` (which already covers Director Blueprints as ordinary `UBlueprint` assets via `subobject:` paths) by adding the Sequencer-specific binding context — Possessable / Spawnable / master tracks and the synthetic `SequenceEvent__ENTRYPOINT*` UFunctions UE generates for "Quick Bind" event entries.

### Action Categories

All 6 actions live in `Source/MonolithLevelSequence/Private/MonolithLevelSequenceActions.cpp`. Counts below are the literal registrations (verified 2026-04-27).

| Category | Actions | Description |
|----------|---------|-------------|
| Smoke | 1 | `ping` — module liveness check, returns `{status:"ok", module:"MonolithLevelSequence"}` |
| Discover | 2 | `list_directors` (all LSes with a Director, optional `asset_path_filter` glob), `get_director_info` (one-LS summary: counts grouped by kind, event-binding totals, sample functions) |
| Inspect | 3 | `list_director_functions` (with `kind` filter: `user` / `custom_event` / `sequencer_endpoint` / `event` alias / `all`), `list_director_variables` (name + K2-formatted type, declaration order), `list_event_bindings` (event-tracks grouped by binding GUID, each with sections + resolved Director function) |
| Reverse-lookup | 1 | `find_director_function_callers` — given a function name, every event-track section across the project that fires it (with binding context) |

**Total:** **7** registered (`ping` + `list_directors` + `get_director_info` + `list_director_functions` + `list_director_variables` + `list_event_bindings` + `find_director_function_callers`).

### Indexer

`FLevelSequenceIndexer` registers itself into `UMonolithIndexSubsystem` on `OnPostEngineInit` (when `bIndexLevelSequences` is true). Implements `IMonolithIndexer::IndexAsset` for `LevelSequence` assets and writes 4 tables:

| Table | Rows per | Purpose |
|-------|---------|---------|
| `level_sequence_directors` | one per LS with a Director BP | Director name + total function/variable counts; LSes with no Director are silently skipped |
| `level_sequence_director_functions` | one per Director's own function | `kind` ∈ {`user`, `custom_event`, `sequencer_endpoint`}; signature stored as JSON array of `{name, type}` |
| `level_sequence_director_variables` | one per Director's own variable | Name + K2-schema-formatted type |
| `level_sequence_event_bindings` | one per `FMovieSceneEvent` (trigger entry or repeater) | Binding GUID + name + kind (possessable / spawnable / master) + bound class + section kind + the Director function it fires (`fires_function_id` resolved via per-asset SQL UPDATE post-pass) |

### Conventions notes

- **Own-functions only.** Inherited base-class methods (e.g. `OnCreated`, `GetBoundActor`) and compiler-generated `ExecuteUbergraph*` dispatchers are deliberately NOT indexed — same convention as `blueprint_query.get_functions`.
- **Synthetic Sequencer endpoints ARE indexed.** When the user clicks "Quick Bind" / "Create New Endpoint" in Sequencer, UE compiles a `SequenceEvent__ENTRYPOINT<DirBP>_N` UFunction with no graph node. We capture these via `TFieldIterator<UFunction>(GenClass)` after walking the graphs — they are the actual runtime targets of `FMovieSceneEvent::Ptrs::Function`, so `event_bindings.fires_function_id` would never resolve without them.
- **No FK on `assets(id)`.** The two tables that reference `ls_asset_id` (`level_sequence_directors` and `level_sequence_event_bindings`) intentionally have no FK to the core `assets(id)` — `FMonolithIndexDatabase::ResetDatabase()` (called by `force=true` reindex) wipes built-in tables without knowing about our custom ones, and a FK would block the `DELETE FROM assets`. Pattern borrowed from `MonolithAI`'s `ai_assets`.
- **Cleanup keyed by `ls_path`.** `IndexAsset` looks up the previous director row by `ls_path` (stable across reindex) and also captures its previous `ls_asset_id` to wipe orphan event-bindings — necessary because the core `assets` autoincrement restarts on full reindex.

### Notes

> **Glob filters.** Both `list_directors` and `find_director_function_callers` accept an `asset_path_filter` parameter; `*` and `?` are converted to SQL `LIKE` `%` and `_` before the query.
>
> **Path format.** All actions that take an `asset_path` expect the full object path (e.g. `/Module/.../File.File`), matching `ULevelSequence::GetPathName()`. A typo or missing-Director path returns a clear error with a hint.

---
