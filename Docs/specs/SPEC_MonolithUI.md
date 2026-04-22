# Monolith — MonolithUI Module

**Parent:** [SPEC_CORE.md](../SPEC_CORE.md)
**Engine:** Unreal Engine 5.7+
**Version:** 0.14.1 (Beta)

---

## MonolithUI

**Dependencies:** Core, CoreUObject, Engine, MonolithCore, UnrealEd, UMGEditor, UMG, Slate, SlateCore, Json, JsonUtilities, KismetCompiler, MovieScene, MovieSceneTracks. Optional: CommonUI, CommonInput (gates `WITH_COMMONUI`)
**Total actions:** 92 — 42 UMG baseline + 50 CommonUI conditional on `#if WITH_COMMONUI`
**Settings toggle:** `bEnableUI` (default: True)

**Build.cs notes — conditional CommonUI:** Build.cs detects CommonUI across 3 install vectors (project Plugins/, engine Plugins/Marketplace/, engine Plugins/Runtime/CommonUI). When found, adds `CommonUI` + `CommonInput` deps and sets `WITH_COMMONUI=1`. Otherwise sets `WITH_COMMONUI=0` and the CommonUI action pack compiles to an empty stub. **Release escape hatch:** `MONOLITH_RELEASE_BUILD=1` env var forces detection off (validates the `WITH_COMMONUI=0` path). Mirrors the `MonolithMesh` (v0.14.1) and `MonolithBABridge` patterns.

### Classes

| Class | Responsibility |
|-------|---------------|
| `FMonolithUIModule` | Registers 42 UMG baseline actions; conditionally invokes `FMonolithCommonUIActions::RegisterAll(Registry)` when `WITH_COMMONUI=1`. Logs the live `ui` namespace action count at startup (no hardcoded total) |
| `FMonolithUIActions` | Widget blueprint CRUD: create, inspect, add/remove widgets, property writes, compile |
| `FMonolithUISlotActions` | Layout slot operations: slot properties, anchor presets, widget movement |
| `FMonolithUITemplateActions` | High-level HUD/menu/panel scaffold templates (8 templates) |
| `FMonolithUIStylingActions` | Visual styling: brush, font, color scheme, text, image, batch style |
| `FMonolithUIAnimationActions` | UMG widget animation CRUD: list, inspect, create, add/remove keyframes |
| `FMonolithUIBindingActions` | Event/property binding inspection, list view setup, widget binding queries |
| `FMonolithUISettingsActions` | Settings/save/audio/input remapping subsystem scaffolding (5 scaffolds) |
| `FMonolithUIAccessibilityActions` | Accessibility subsystem scaffold, audit, colorblind mode, text scale |
| `FMonolithCommonUIActions` | Public aggregator (header in `Public/CommonUI/`) — `RegisterAll(Registry)` registers all 50 CommonUI actions across 9 categories. Whole class disappears when `WITH_COMMONUI=0` |
| `MonolithCommonUI::*` (helpers) | Shared Pattern 1/2/3 helpers in `Private/CommonUI/MonolithCommonUIHelpers.{h,cpp}`: `CreateAsset`, `LoadWidgetForMutation`, `CompileAndSaveWidgetBlueprint`, `InvokeRuntimeFunction`, `SetPropertyFromJson`, `GetPIEWorld` |

### CommonUI Action Pack (50 actions, conditional on `WITH_COMMONUI`)

> **Status:** Shipped v0.14.0 (M0.5). 9 categories, 50 actions. All gated on `#if WITH_COMMONUI`. Filter via `monolith_discover({namespace:"ui", category:"CommonUI"})`. Backwards-compatible `RegisterAction` signature carries the new `Category` field; each action emits `category` in `discover()` output JSON.

**A: Activatable Lifecycle (8)** — `create_activatable_widget`, `create_activatable_stack`, `create_activatable_switcher`, `configure_activatable`, `push_to_activatable_stack` [RUNTIME], `pop_activatable_stack` [RUNTIME], `get_activatable_stack_state` [RUNTIME], `set_activatable_transition`

**B: Buttons + Styling (9)** — `convert_button_to_common`, `configure_common_button`, `create_common_button_style`, `create_common_text_style`, `create_common_border_style`, `apply_style_to_widget`, `batch_retheme`, `configure_common_text`, `configure_common_border`

**C: Input / Actions / Glyphs (7)** — `create_input_action_data_table`, `add_input_action_row`, `bind_common_action_widget`, `create_bound_action_bar`, `get_active_input_type` [RUNTIME], `set_input_type_override` [RUNTIME], `list_platform_input_tables`

**D: Navigation / Focus (5)** — `set_widget_navigation`, `set_initial_focus_target`, `force_focus` [RUNTIME], `get_focus_path` [RUNTIME], `request_refresh_focus` [RUNTIME]

**E: Lists / Tabs / Groups (7)** — `setup_common_list_view`, `create_tab_list_widget`, `register_tab` [RUNTIME], `create_button_group` [RUNTIME], `configure_animated_switcher`, `create_widget_carousel`, `create_hardware_visibility_border`

**F: Numeric / Rotator / Lazy (4)** — `configure_numeric_text`, `configure_rotator`, `create_lazy_image`, `create_load_guard`

**G: Dialog / Modal (2)** — `show_common_message` [RUNTIME], `configure_modal_overlay`

**H: Audit + Lint (4)** — `audit_commonui_widget`, `export_commonui_report`, `hot_reload_styles` [RUNTIME, EXPERIMENTAL], `dump_action_router_state` [RUNTIME, EXPERIMENTAL]

**I: Accessibility (4)** — `enforce_focus_ring`, `wrap_with_reduce_motion_gate`, `set_text_scale_binding`, `apply_high_contrast_variant`

> **Known limitations (shipped, documented):** `convert_button_to_common` does NOT auto-transfer UButton children to UCommonButtonBase (UCommonButtonBase uses internal widget tree, not AddChild — callers must rewire manually). `set_initial_focus_target` requires the target WBP to expose a `DesiredFocusTargetName` or `InitialFocusTargetName` FName UPROPERTY and override `NativeGetDesiredFocusTarget`.

### Actions — UMG Baseline (42 — namespace: "ui")

**Widget CRUD (7)**
| Action | Params | Description |
|--------|--------|-------------|
| `create_widget_blueprint` | `save_path`, `parent_class` | Create a new Widget Blueprint asset |
| `get_widget_tree` | `asset_path` | Get the full widget hierarchy tree |
| `add_widget` | `asset_path`, `widget_class`, `parent_slot` | Add a widget to the widget tree |
| `remove_widget` | `asset_path`, `widget_name` | Remove a widget from the widget tree |
| `set_widget_property` | `asset_path`, `widget_name`, `property_name`, `value` | Set a property on a widget via reflection |
| `compile_widget` | `asset_path` | Compile the Widget Blueprint and return errors/warnings |
| `list_widget_types` | none | List all available widget classes that can be instantiated |

**Slot Operations (3)**
| Action | Params | Description |
|--------|--------|-------------|
| `set_slot_property` | `asset_path`, `widget_name`, `property_name`, `value` | Set a layout slot property (padding, alignment, size, etc.) |
| `set_anchor_preset` | `asset_path`, `widget_name`, `preset` | Apply an anchor preset to a Canvas Panel slot |
| `move_widget` | `asset_path`, `widget_name`, `new_parent`, `slot_index` | Move a widget to a different parent slot |

**Templates (8)**
| Action | Params | Description |
|--------|--------|-------------|
| `create_hud_element` | `save_path`, `element_type` | Scaffold a common HUD element (health bar, crosshair, ammo counter, etc.) |
| `create_menu` | `save_path`, `menu_type` | Scaffold a menu Widget Blueprint (main menu, pause menu, etc.) |
| `create_settings_panel` | `save_path` | Scaffold a settings panel with common option categories |
| `create_dialog` | `save_path`, `dialog_type` | Scaffold a dialog Widget Blueprint (confirmation, info, input prompt) |
| `create_notification_toast` | `save_path` | Scaffold a notification/toast Widget Blueprint |
| `create_loading_screen` | `save_path` | Scaffold a loading screen Widget Blueprint with progress bar |
| `create_inventory_grid` | `save_path`, `columns`, `rows` | Scaffold a grid-based inventory Widget Blueprint |
| `create_save_slot_list` | `save_path` | Scaffold a save slot list Widget Blueprint |

**Styling (6)**
| Action | Params | Description |
|--------|--------|-------------|
| `set_brush` | `asset_path`, `widget_name`, `brush_property`, `texture_path` | Set a brush/image property on a widget |
| `set_font` | `asset_path`, `widget_name`, `font_asset`, `size` | Set the font and size on a text widget |
| `set_color_scheme` | `asset_path`, `color_map` | Apply a color scheme (name→LinearColor map) across the widget |
| `batch_style` | `asset_path`, `style_operations` | Apply multiple styling operations in a single transaction |
| `set_text` | `asset_path`, `widget_name`, `text` | Set display text on a text widget |
| `set_image` | `asset_path`, `widget_name`, `texture_path` | Set the texture on an image widget |

**Animation (5)**
| Action | Params | Description |
|--------|--------|-------------|
| `list_animations` | `asset_path` | List all UMG animations on a Widget Blueprint |
| `get_animation_details` | `asset_path`, `animation_name` | Get tracks and keyframes for a named animation |
| `create_animation` | `asset_path`, `animation_name` | Create a new UMG widget animation |
| `add_animation_keyframe` | `asset_path`, `animation_name`, `widget_name`, `property`, `time`, `value` | Add a keyframe to a widget animation track |
| `remove_animation` | `asset_path`, `animation_name` | Remove a UMG widget animation |

**Bindings (4)**
| Action | Params | Description |
|--------|--------|-------------|
| `list_widget_events` | `asset_path` | List all bindable events on a Widget Blueprint |
| `list_widget_properties` | `asset_path`, `widget_name` | List all bindable properties on a widget |
| `setup_list_view` | `asset_path`, `list_view_name`, `entry_widget_path` | Configure a List View widget with an entry widget class |
| `get_widget_bindings` | `asset_path` | Get all active property and event bindings on a Widget Blueprint |

**Settings Scaffolding (5)**
| Action | Params | Description |
|--------|--------|-------------|
| `scaffold_game_user_settings` | `save_path`, `class_name` | Scaffold a UGameUserSettings subclass with common settings properties |
| `scaffold_save_game` | `save_path`, `class_name` | Scaffold a USaveGame subclass with save slot infrastructure |
| `scaffold_save_subsystem` | `save_path`, `class_name` | Scaffold a save game subsystem (UGameInstanceSubsystem) |
| `scaffold_audio_settings` | `save_path`, `class_name` | Scaffold an audio settings manager with volume/mix controls |
| `scaffold_input_remapping` | `save_path`, `class_name` | Scaffold an input remapping system backed by Enhanced Input |

**Accessibility (4)**
| Action | Params | Description |
|--------|--------|-------------|
| `scaffold_accessibility_subsystem` | `save_path`, `class_name` | Scaffold a UGameInstanceSubsystem implementing accessibility features |
| `audit_accessibility` | `asset_path` | Audit a Widget Blueprint for common accessibility issues (missing tooltips, low contrast, small text) |
| `set_colorblind_mode` | `asset_path`, `mode` | Apply a colorblind-safe palette mode (deuteranopia, protanopia, tritanopia) |
| `set_text_scale` | `asset_path`, `scale` | Apply a global text scale factor to all text widgets in the blueprint |

---
