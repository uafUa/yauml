# uuml Productization Plan

Status: command foundation and Phase 3.1–3.5 implemented; project and
presentation styling is next

The product owner accepted the MVP on 2026-07-21. This plan turns the broader
architecture roadmap into bounded, testable delivery tranches. MVP audit notes
remain useful regression input, but they no longer reopen the MVP boundary.

Delivery priority after the accepted MVP is:

1. Complete the manual-modeling UI and diagram-productivity features.
2. Implement C++ import and repeatable synchronization.
3. Scale, recovery, and release hardening.
4. Diagram export, which is useful but not currently a product priority.

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
- Alignment, equal sizing, minimum-gap edge-to-edge distribution, and keyboard
  nudging are implemented through diagram-local actions and shortcuts. The
  distribution fallback gap is an application preference persisted through
  the settings service.
- Live move snapping is implemented for the configurable grid and for other
  elements' left/center/right and top/center/bottom features. Matching features
  display themed alignment guides, multi-selections retain their internal
  geometry, and holding Alt temporarily suppresses snapping. Grid spacing and
  both snapping modes are persisted application preferences.
- Treat a multi-object operation as one undoable transaction.

### 3.5 Connector routing and direct interaction — implemented

- Straight and orthogonal connector shapes are implemented. The default shape
  for newly created connectors is a persisted application preference and
  defaults to **Straight**; individual connector presentations retain their
  chosen routing mode in JSON5. The relationship context menu changes routing
  through an undoable command. Orthogonal paths contain only horizontal and
  vertical segments, preserve persisted user bend handles, and recompute their
  automatic elbows when endpoint nodes or ports move. Basic Manhattan routing
  is the current acceptance level; obstacle avoidance remains a separate
  enhancement.
- The structural relationship vocabulary for supported class and package
  diagrams is implemented: dependency, generalization/inheritance,
  realization/implementation, navigable association, aggregation, and
  composition. Each type has distinct persisted semantics, default text, line
  style, endpoint decoration, creation actions, and undo/redo. Aggregation and
  composition use the source as the whole end; the other directed types point
  toward the target. Behavioral-diagram connectors remain outside the current
  product direction.
- Edge-gesture connector creation is implemented. While the pointer button is
  held on a node edge, pressing the relationship hotkey enters a live
  drag-to-connect preview using the configured default routing. Defaults are
  `D` dependency, `I` implementation/realization, `H` inheritance/
  generalization, `A` association, `G` aggregation, and `C` composition. The
  initial press and final drop positions become exact persisted perimeter
  attachments. Releasing over any node—including the originating node—commits
  one relationship command; self-connections receive a persisted outside loop.
  Escape or a drop on empty space discards the candidate without changing the
  model or undo history.
- Direct endpoint manipulation is implemented and replaces the modal
  **Reconnect source…** and **Reconnect target…** actions. A selected connector
  exposes fully visible draggable endpoint handles. Starting a drag
  provisionally detaches that end; hovering a node snaps to its exact perimeter,
  and releasing commits one compact reconnection command. Dragging around the
  current node remains a smaller port-move command. Any node—including the other
  endpoint—is valid, and a new self-connection receives a persisted outside
  loop. Escape and empty-space drops restore the original connection without
  touching model or undo state.
- The Connectors preferences page implements both the default connector shape
  and editors for all six relationship gesture keys. Assignments are applied
  atomically, normalized to uppercase, restricted to one letter or digit, and
  rejected visibly when empty or duplicated. All values persist as application
  settings.
- Keep routing mode, semantic direction, endpoint attachments, bend constraints,
  and self-connections intact across save/load and exact command undo/redo.

### 3.6 Project and presentation styling

- The application palette is centralized behind semantic roles shared by QML
  controls and the native scene-graph renderer. All roles are editable through
  the persisted Colors preferences page, with staged apply/cancel behavior and
  render-thread-safe palette snapshots.
- Introduce project styles and presentation-local overrides behind stable style
  interfaces.

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

## Phase 6: diagram export — lower priority

- Add PNG and PDF export through the shared diagram renderer after
  rendered-output regression tests exist.
- Add SVG export only when there is concrete product demand; it remains outside
  the critical productization path.

## Release gates for every tranche

- Human-maintainable implementation with cohesive responsibilities and comments
  for invariants and non-obvious tradeoffs.
- Focused command/undo/persistence tests for new model state.
- Successful MSVC Release build and complete CTest pass.
- Checklist or manual scenario for behavior that cannot yet be automated.
