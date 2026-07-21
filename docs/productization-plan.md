# uuml Productization Plan

Status: command foundation and Phase 3.1–3.3 implemented; Phase 3.4 is next

The product owner accepted the MVP on 2026-07-21. This plan turns the broader
architecture roadmap into bounded, testable delivery tranches. MVP audit notes
remain useful regression input, but they no longer reopen the MVP boundary.

## Product interaction direction

- Keep the main toolbar focused on project/session commands.
- Move diagram-specific creation and editing commands into diagram context
  menus. This removes ambiguity when several top-level diagram windows are
  visible and clicking the main toolbar changes window focus.
- Keep every command reachable by keyboard as context menus are introduced.
- Continue routing all mutations through the shared C++ command layer.

## Productization foundation: command transactions — implemented

This foundation precedes further Phase 3 feature work. The MVP undo mechanism
captured complete projects, which was useful for proving behavior but would make
history memory grow with total project size rather than the size of each edit.

- Represent each committed user gesture as a concrete polymorphic
  `ProjectCommand` on one shared history stack.
- Prepare and validate a command before pushing it. Every command implements its
  own `execute()` and `revert()` behavior and retains only the records or values
  required by that operation.
- Never copy or diff complete `ProjectData` values to construct undo history.
- Apply execute, undo, and redo through the same commit-and-notify boundary.
- Preserve clean/dirty position and expose command descriptions for UI labels.
- Verify exact forward/undo/redo equivalence for property, structural, cascade,
  presentation, and multi-record commands on both ordinary and large models.

## Phase 3: manual-modeling productivity

### 3.1 Persisted diagram workspace — implemented

- Restore main and detached tab groups per project.
- Restore tab order, detached-window geometry, and the active diagram.
- Restore main-window geometry and side-panel visibility and widths.
- Debounce geometry persistence while windows are moving.
- Ignore removed diagram IDs and recover safely when monitor geometry changes.

### 3.2 Diagram context menus and command routing — implemented

- Canvas, element, connector, and tab context menus own their applicable
  diagram commands.
- Element creation, relationship creation, reconnection, presentation removal,
  fit, and diagram deletion moved out of global diagram controls.
- Window-local shortcuts invoke the active visible `DiagramView`, independently
  of menu placement.
- Context creation retains the clicked diagram position, while explicit
  diagram and relationship deletion APIs avoid focus-derived targets.

### 3.3 Connector editing — implemented

- Connector presentations own ordered, persisted bend points with retained
  unknown fields for forward-compatible files.
- Context menus and connector double-click add bend points; selected bend
  handles support direct drag, Delete, individual removal, and route clearing.
- Polyline-aware rendering, arrowheads, labels, hit testing, and fit-to-content
  follow edited routes.
- Endpoint and bend-point state is preserved through node move/resize,
  reconnect, save/load, and command undo/redo, with core and canvas regression
  tests.

### 3.4 Arrangement tools

- Rectangular lasso selection is implemented as the multi-object interaction
  foundation. Plain drag replaces selection, Shift adds, and Ctrl toggles;
  selection-only frames retain text atlases for large-diagram responsiveness.
- Alignment, equal sizing, distribution, snapping guides, and keyboard nudging.
- Treat a multi-object operation as one undoable transaction.

### 3.5 Styling and export

- Introduce project styles and presentation-local overrides behind stable style
  interfaces.
- Add SVG, PNG, and PDF export after rendered-output regression tests exist.

## Phase 4: C++ import and synchronization

- Compilation-database discovery and Clang AST indexing.
- Change-set preview, source bindings, rename matching, provenance, and
  user-authoritative conflict handling.
- Shared GUI/headless import services and structured conflict diagnostics.

## Phase 5: scale and hardening

- Incremental loading/indexing, schema migrations, merge diagnostics, and
  expanded recovery testing.
- Performance benchmarks and rendered-image regression coverage.
- Windows packaging automation followed by cross-platform packaging.

## Release gates for every tranche

- Human-maintainable implementation with cohesive responsibilities and comments
  for invariants and non-obvious tradeoffs.
- Focused command/undo/persistence tests for new model state.
- Successful MSVC Release build and complete CTest pass.
- Checklist or manual scenario for behavior that cannot yet be automated.
