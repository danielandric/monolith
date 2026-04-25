# Monolith Module Testing Plan

**Date:** 2026-04-25
**Scope:** MonolithAudio (81), MonolithAI (229), MonolithComboGraph (13), MonolithGAS (re-verify), MonolithLogicDriver (re-verify), conditional compilation
**Excluded:** MonolithMesh (tested separately)

---

## Phase 0 — Conditional Compilation (ALL modules, 1 session)

**Goal:** Verify `MONOLITH_RELEASE_BUILD=1` compiles clean with ALL optional deps OFF.

1. Close editor
2. `$env:MONOLITH_RELEASE_BUILD="1"` + UBT build with `-DisableUnity`
3. Verify zero errors across all modules:
   - `WITH_COMMONUI=0` (MonolithUI CommonUI actions)
   - `WITH_GBA=0` (MonolithGAS)
   - `WITH_COMBOGRAPH=0` (MonolithComboGraph)
   - `WITH_LOGICDRIVER=0` (MonolithLogicDriver)
   - `WITH_METASOUND=0` (MonolithAudio MetaSound subset)
   - `WITH_STATETREE=0`, `WITH_SMARTOBJECTS=0` (MonolithAI)
   - `WITH_BLUEPRINTASSIST=0` (MonolithBABridge)
4. Unset env var, rebuild dev binaries, relaunch editor
5. Verify `monolith_status` shows full action count (1442)

**Pass criteria:** Zero compile errors in release mode. Full action count restored in dev mode.

---

## Phase 1 — MonolithComboGraph (13 actions, 1 session)

**Prereqs:** ComboGraph plugin installed (`WITH_COMBOGRAPH=1`)

### 1a. Discovery & Read (4 actions)
- `list_combo_graphs` — verify returns any existing combo graphs
- `get_combo_graph_info` — inspect a graph's nodes/edges
- `get_combo_node_effects` — read effects on a node
- `validate_combo_graph` — run validation, check output format

### 1b. Create & Write (5 actions)
- `create_combo_graph` at `/Game/Tests/Monolith/ComboGraph/CG_Test`
- `add_combo_node` — add 3 nodes (entry, light, heavy)
- `add_combo_edge` — wire entry→light→heavy
- `set_combo_node_effects` — apply a gameplay effect reference
- `set_combo_node_cues` — apply a montage/cue reference

### 1c. Scaffold (3 actions)
- `create_combo_ability` — scaffold a GAS ability for the test combo
- `link_ability_to_combo_graph` — wire them together
- `scaffold_combo_from_montages` — test with existing project montages

### 1d. Error Paths (3-5 tests)
- Operations on non-existent graphs
- Invalid edge connections (self-loop, duplicate)
- scaffold_combo_from_montages with invalid montage paths

**Pass criteria:** 13/13 actions respond correctly (success or clean error). Test assets in `/Game/Tests/Monolith/ComboGraph/`.

---

## Phase 2 — MonolithAudio (81 actions, 2-3 sessions)

**Prereqs:** MetaSound plugin available (`WITH_METASOUND=1`)

### 2a. Session 1: Core CRUD & Batch (35 actions)

**Asset CRUD (15):**
- Create one of each: SoundAttenuation, SoundClass, SoundMix, SoundConcurrency, SoundSubmix
- Get/inspect each created asset
- Duplicate, rename, delete

**Query & Search (10):**
- Hierarchy inspection (class→submix chains)
- Reference queries (what uses this attenuation?)
- Audio health checks

**Batch Operations (10):**
- Template application across a test folder
- Batch property changes

### 2b. Session 2: Sound Cue Graph (21 actions)

**Sound Cue CRUD:**
- Create a Sound Cue at `/Game/Tests/Monolith/Audio/SC_Test`
- Add nodes (Wave Player, Random, Mixer, Attenuation)
- Wire nodes together
- `build_sound_cue_from_spec` — power action test with a spec JSON

**Templates (5):**
- Create all 5 template cues: random, layered, looping, crossfade, switch
- Verify each produces a valid, compilable cue

**Validation + Preview:**
- Validate each created cue
- Preview (if supported in editor without PIE)
- Delete test cues

### 2c. Session 3: MetaSound Graph (25 actions)

**MetaSound CRUD:**
- Create MetaSound Source + Patch
- Add nodes via Builder API
- Wire nodes
- `build_metasound_from_spec` — power action test

**Templates (4):**
- Create all 4 templates: oneshot, ambient, synth, interactive
- Verify each compiles

**Variables + Presets:**
- Create/get/set MetaSound variables
- Create preset from existing MetaSound

**Error Paths:**
- Operations with `WITH_METASOUND=0` path (should error gracefully)

**Pass criteria:** 81/81 actions respond correctly. Test assets in `/Game/Tests/Monolith/Audio/`.

---

## Phase 3 — MonolithAI (229 actions, 4-5 sessions)

**Prereqs:** State Trees and Smart Objects enabled. Mass Entity/Zone Graph optional.

### 3a. Session 1: Behavior Trees (40-50 actions)
- BT CRUD: create, list, get info, delete
- Node management: add tasks, decorators, services, composites
- `build_behavior_tree_from_spec` — crown jewel test
- Blackboard: create, add/remove keys, get/set values
- Wire BT to AI Controller

### 3b. Session 2: State Trees + EQS (50-60 actions)
- State Tree CRUD: create, list, get info
- State/transition management
- `build_state_tree_from_spec` — crown jewel test
- EQS: create query, add generators/tests, run query
- Smart Objects: create definition, add slots, configure

### 3c. Session 3: Controllers + Perception + Navigation (40-50 actions)
- AI Controller: create, configure, assign BT/ST
- Perception: configure senses (sight, hearing, damage), get config
- Navigation: navmesh queries, path finding, nav modifiers
- Runtime/PIE actions (if PIE available): spawn AI, inspect active BT

### 3d. Session 4: Scaffolds + Discovery + Advanced (40-50 actions)
- Scaffold templates: patrol, guard, investigate, flee, etc.
- Discovery: search AI assets, explain behaviors, compare
- Advanced: hierarchical task networks, utility AI scoring

### 3e. Session 5: Remaining + Error Paths (30-40 actions)
- Any actions not covered in sessions 1-4
- Comprehensive error path testing
- Cross-module integration (AI + GAS, AI + LogicDriver)

**Pass criteria:** 229/229 actions respond correctly. Test assets in `/Game/Tests/Monolith/AI/`.

---

## Phase 4 — Re-verify Previously Tested Modules (1 session)

Quick smoke tests — these were tested before but confirm nothing has regressed.

### 4a. MonolithGAS (130 actions) — Spot check 10-15 key actions
- Create attribute set, gameplay effect, ability
- Grant ability, apply effect, inspect ASC
- Runtime PIE actions (if available)

### 4b. MonolithLogicDriver (66 actions) — Spot check 8-10 key actions
- Create state machine, add states/transitions
- `build_sm_from_spec`, validate, export
- Runtime PIE actions

### 4c. MonolithCommonUI (50 actions) — Already fully tested this session
- Skip unless regressions suspected

**Pass criteria:** Key actions from each module still work. No regressions.

---

## Phase 5 — Cross-Module Integration (1 session)

Test workflows that span multiple modules:

1. **GAS + ComboGraph:** Create ability → link to combo graph → validate both
2. **AI + GAS:** Create BT with GAS ability task nodes → AI Controller with ASC
3. **AI + LogicDriver:** Create SM-driven AI behavior → wire to AI Controller
4. **Audio + AI:** Create perception-triggered sound cues → configure AI hearing sense
5. **UI + GAS:** Create HUD elements bound to GAS attributes (health bar from AttributeSet)

**Pass criteria:** Each cross-module workflow completes without errors.

---

## Execution Notes

- **All test assets** go in `/Game/Tests/Monolith/<ModuleName>/` (disposable)
- **Sequential MCP calls** — never parallel (learned from CommonUI crash)
- **Document bugs immediately** — update TODO.md in same session as fixes
- **Agents can research freely** but need approval before editing
- **Phase 0 first** — conditional compilation must pass before functional testing
- **Each phase produces:** pass/fail count, bug list, fix list, updated TODO.md

---

## Estimated Effort

| Phase | Sessions | Actions | Priority |
|-------|----------|---------|----------|
| 0 — Conditional Compilation | 1 | All | P0 |
| 1 — ComboGraph | 1 | 13 | P1 |
| 2 — Audio | 2-3 | 81 | P1 |
| 3 — AI | 4-5 | 229 | P2 |
| 4 — Re-verify | 1 | ~25 spot | P3 |
| 5 — Cross-Module | 1 | ~10 workflows | P3 |
| **Total** | **10-12** | **~358** | |
