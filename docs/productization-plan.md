# uuml Productization Plan

Status: command foundation, the Phase 3 core, relationship annotations, and
stereotypes are implemented; contextual toolbox expansion remains in progress.
Phase 4 source import and relationship inference are implemented, with
synchronization hardening in progress. Phase 5 release and persistence
hardening is underway

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
  Empty container interiors behave as diagram workspace for lasso gestures;
  container movement is intentionally restricted to the frame header and
  resizing to its bottom-right handle.
- Alignment, equal sizing, minimum-gap edge-to-edge distribution, and keyboard
  nudging are implemented through diagram-local actions and shortcuts. The
  distribution fallback gap is an application preference persisted through
  the settings service.
- Live move snapping is implemented for the configurable grid and for other
  elements' left/center/right and top/center/bottom features. Matching features
  display themed alignment guides, multi-selections retain their internal
  geometry, and holding Alt temporarily suppresses snapping. Grid spacing and
  both snapping modes are persisted application preferences.
- A persisted **Diagram item sizing** preference chooses between the traditional
  fixed 220 × 120 rectangle and a rectangle measured from visible text when
  items are added from the project tree. Bulk drops use compact content-aware
  spacing. Diagram context menus expose an undoable **Fit to content** action
  for both elements and container frames.
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
  realization/implementation, navigable association, aggregation, composition,
  and containment/nesting. Each type has distinct persisted semantics, default
  text, line style, endpoint decoration, creation actions, and undo/redo.
  Aggregation, composition, and containment use the source as the whole/owner
  end; the other directed types point toward the target. Behavioral-diagram
  connectors remain outside the current product direction.
- Edge-gesture connector creation is implemented. While the pointer button is
  held on a node edge, pressing the relationship hotkey enters a live
  drag-to-connect preview using the configured default routing. Defaults are
  `D` dependency, `I` implementation/realization, `H` inheritance/
  generalization, `A` association, `G` aggregation, `C` composition, and `N`
  containment. The initial press and final drop positions become exact
  persisted perimeter attachments. Releasing over any node—including the
  originating node—commits one relationship command; self-connections receive
  a persisted outside loop. Escape or a drop on empty space discards the
  candidate without changing the model or undo history.
- Direct endpoint manipulation is implemented and replaces the modal
  **Reconnect source…** and **Reconnect target…** actions. A selected connector
  exposes fully visible draggable endpoint handles. Starting a drag
  provisionally detaches that end; hovering a node snaps to its exact perimeter,
  and releasing commits one compact reconnection command. Dragging around the
  current node remains a smaller port-move command. Any node—including the other
  endpoint—is valid, and a new self-connection receives a persisted outside
  loop. Escape and empty-space drops restore the original connection without
  touching model or undo state.
- Connector ends magnetically snap to presentation-local port points during
  creation and endpoint dragging. Top/bottom share one configurable odd count;
  left/right share another, with one centered point per side by default.
  Available points are shown only during connector interaction, Alt preserves
  fully free perimeter placement, and relative offsets keep snapped ends on
  their points as presentations resize. Counts are persisted and changed
  through one undoable presentation command.
- The Connectors preferences page implements both the default connector shape
  and editors for all seven relationship gesture keys. Assignments are applied
  atomically, normalized to uppercase, restricted to one letter or digit, and
  rejected visibly when empty or duplicated. All values persist as application
  settings.
- Keep routing mode, semantic direction, endpoint attachments, bend constraints,
  and self-connections intact across save/load and exact command undo/redo.

### 3.6 Hierarchical project browser and diagram containers — implemented

- Extended selection and native cross-window drag/drop are implemented for
  model types. Ctrl toggles rows, Shift selects ranges, and one drop creates a
  deterministic grid at the pointer. Existing presentations are skipped,
  newly eligible semantic connectors appear, and the complete drop is one
  compact undo command.
- Qualified model names are projected as namespace and nested-type hierarchy in
  the browser. Namespace and owning-type drags expand recursively to a stable,
  de-duplicated list of contained model types; selecting both a container and
  one of its children never creates duplicate presentations.
- Custom browser folders are persisted as project data and can be created at
  the model root or inside namespaces, types, and other folders. Native tree
  drag/drop reorganizes selected elements or folders with cycle protection;
  create, rename, move, and delete are compact undoable commands. Deleting a
  folder promotes its direct contents instead of deleting semantic model data.
  Delete is available from the tree context menu and applies atomically to the
  complete extended selection. Persisted cross-type sibling ordering can be
  changed by dragging one or more selected siblings to the top or bottom edge
  of another row; an insertion rail previews the destination. Dropping in the
  center of a container retains the existing move-into behavior. The
  context-menu **Move up** and **Move down** actions remain available as an
  alternative.
- Represent semantic ownership independently from browser organization. A C++
  namespace is imported as the corresponding UML package; there is no parallel
  namespace container kind. UML packages and nested types participate in
  qualified identity, while custom folders do not. A browser folder therefore
  stores only grouping and ordering metadata and may be nested at any visible
  semantic level without changing source bindings or UML meaning.
- Resolve every tree drag to a de-duplicated set of leaf subjects. Dragging a
  namespace, package, custom folder, or type with nested types includes all its
  descendants; overlapping selections place each subject once.
- Custom-folder container presentations are implemented as explicit persisted
  frames that own child presentation IDs and reject missing, multiply-owned,
  or cyclic membership. Dropping a folder creates its nested folder frames,
  missing leaf presentations, and eligible connectors through one compact undo
  command. Frames render below their descendants; moving a frame moves its
  complete subtree, while resizing changes only the frame. Removing a frame or
  deleting its browser folder promotes its children without deleting semantic
  elements. Frame colors participate in the centralized theme editor.
- Boundary editing is implemented: the innermost eligible frame at the
  completed drag's release point becomes the explicit owner. Dropping at the
  diagram root removes ownership, nested frames can be detached or reparented,
  and moving frames exclude their own subtree as a target to prevent cycles.
  Geometry and membership changes share one compact undoable command;
  incidental rectangle overlap never changes ownership.
- UML package frames are implemented with conventional tabbed rendering and the
  same explicit, persisted membership mechanism as custom-folder frames.
  Dragging a package from the tree creates nested package frames, content-sized
  leaf presentations, and eligible connectors as one undoable command. A
  diagram may show only a chosen subset; later source imports do not silently
  add presentations. A package can also be added explicitly as an empty frame.
  **Wrap in parent namespace** creates or reuses the immediate semantic parent
  frame when an element is currently shown at diagram root, in an ancestor
  namespace, or in another presentation container.
- Diagram containment is always presentation-only. Moving presentations into,
  between, or out of package frames never changes semantic package ownership.
  An element may be dropped only at diagram root, in its immediate package, or
  in an ancestor package; unrelated and descendant package frames reject the
  drop because their visual containment would be semantically misleading.
  Labels show the full qualified name at diagram root, a relative qualified
  name in an ancestor package, and the short name in the immediate package.
- The persisted **Project-tree containment changes by drag and drop**
  preference provides **Disallow**, **Ask** (default), and **Allow** policies.
  It governs package reassignment and moving a classifier under another class
  or struct as a nested type in the project browser only. Approved moves change
  browser placement, package assignment, and stable enclosing-type ownership
  atomically; cancellation changes nothing.

### 3.7 Project and presentation styling — implemented

- The application palette is centralized behind semantic roles shared by QML
  controls and the native scene-graph renderer. All roles are editable through
  the persisted Colors preferences page, with staged apply/cancel behavior and
  render-thread-safe palette snapshots.
- Each project owns a registry of named diagram styles. A style has a stable
  UUID, a unique editable name, and generic fill, header, border, primary-text,
  secondary-text, and divider colors usable by classifier nodes and container
  frames. Connector styling remains a separate future slice.
- Styles can be assigned from project-tree and diagram context menus. The
  effective style resolves in the order presentation override, semantic/browser
  subject override, nearest styled project-browser ancestor, then the
  application palette. The project style registry can also be opened directly
  from the main Edit menu without requiring a selected element. Package
  elements store namespace styles; legacy synthetic namespace rows retain
  assignments by qualified path.
- Creating, editing, assigning, deleting, and clearing styles are undoable.
  Deletion confirms its assignment count and clears references so affected
  presentations resume inheritance. The registry and every assignment persist
  in project JSON5 rather than application preferences.

### 3.8 Contextual hover toolboxes — relationship and multi-selection
toolboxes implemented, expansion planned

- Keep contextual toolboxes task-specific instead of building one universal
  floating toolbar. The implemented selected-edge toolbox remains dedicated to
  starting relationships. Later providers cover a selected node or container,
  a multi-selection, and a selected connector, each exposing only commands
  applicable to that target and selection state.
- Reuse the centralized command/action catalog so toolbox buttons, context
  menus, keyboard commands, accessible names, and SVG icons invoke the same
  command definitions. Do not duplicate mutation logic in QML.
- The first expansion slice is implemented. Hovering any member of a
  multi-selection exposes all alignment, distribution, and equal-sizing
  commands in a compact palette anchored to the particular selected
  presentation under the pointer. Hovering a selected rectangle edge still
  gives relationship creation priority. The palette invokes the existing
  command-backed actions and follows its hovered presentation as repeated
  arrangement commands change the geometry.
- The next expansions add connector routing and annotation controls, followed
  by high-frequency node/container commands such as edit name, fit to content,
  style, and wrap in parent namespace. Destructive and modal commands remain in
  context menus until usage proves that placing them in a hover surface is
  beneficial.
- Use one shared visibility state machine: the toolbox appears only for the
  currently selected target, remains open while the pointer crosses a
  hover-safe bridge to the toolbox, stays open during a drag gesture, and
  dismisses on Escape, selection change, diagram deactivation, or a short
  pointer-leave delay. Keyboard focus and touch/pen invocation must not depend
  on hover.
- Implement the remaining providers in this order: connector
  annotations/routing, node/container commands, then user customization. Each
  slice includes multi-window focus, zoom, clipping, accessibility, and command
  undo/redo tests.

### 3.9 Relationship ends, movable annotations, and stereotypes — implemented

- Explicit semantic source and target relationship-end records are implemented
  and persisted with unknown-field retention. Each end
  has optional `role` and `multiplicity` text; the UI describes multiplicity as
  UML cardinality and accepts conventional values such as `1`, `0..1`, `*`, and
  `1..*` without restricting projects to those examples. These values belong
  to the relationship and therefore appear consistently on every diagram.
- Relationship names, both roles, and both multiplicities are rendered as
  sharp scene-graph text using deterministic route-relative automatic
  placement for straight, orthogonal, and manually bent connectors. They are
  editable in the selected-relationship property panel and in place on the
  diagram. Edits—including clearing optional values—use field-sized undo
  commands. Relationship stereotypes use the same annotation system.
- Treat the relationship name, stereotype, both roles, and both
  multiplicities as connector annotations. Their text is semantic model data,
  while their position is a per-diagram `ConnectorPresentation` concern.
  Empty annotations are neither rendered nor serialized as presentation
  placements.
- Persist annotation positions relative to the routed connector rather than as
  fragile absolute scene coordinates. A placement records normalized distance
  along the route plus tangent and normal offsets. Default placements put
  roles and multiplicities near their corresponding ends and the stereotype
  and name near the route midpoint. Dragging an annotation creates one compact
  presentation command; **Reset position** returns an annotation, or all
  annotations, to automatic layout. Re-routing projects existing placements
  onto the new path so manual layout remains visually stable.
- Add a stereotype catalog with stable identifiers. New projects seed their
  project-owned catalog with conventional UML definitions; after creation,
  those defaults are editable and deletable like entries created by the user.
  The seed includes `local`, `private`, and `api` source-visibility
  classifications.
  Definitions, applicability, and assignments live in project JSON5. Semantic
  elements—packages, classes, structs, enumerations, and nested types—and
  relationships can reference zero or more catalog entries. Custom
  project-browser folders remain organizational rather than UML entities and
  do not acquire semantic stereotypes.
- Render stereotypes conventionally as `«name»`. Element stereotypes appear
  above the element name; relationship stereotypes are movable connector
  annotations. The properties panel, context actions, and in-place diagram
  interaction open the same checkable dropdown, filtering the project catalog
  by applicability and committing all checkbox changes as one undo command
  when it closes. A separate project stereotype manager supports create,
  rename, applicability, and delete-with-usage-confirmation workflows.
- Implement edits with field-sized polymorphic commands:
  relationship-metadata commands retain only the changed endpoint values,
  stereotype commands retain assignment IDs, and annotation-move commands
  retain only the before/after placement. Source synchronization keeps manual
  roles, multiplicities, and stereotype assignments user-authoritative unless
  a later import rule explicitly owns one of those fields.
- The complete slice is implemented: semantic endpoint data, automatic
  rendering and property editing, route-relative drag/reset placement, and the
  seeded project-owned stereotype catalog with dropdown assignment workflows.
  Persistence, schema migration, command undo/redo, validation, canvas
  interaction, and application smoke tests protect the delivered behavior.

## Phase 4: C++ import and synchronization — in progress

- Folder-first libclang AST indexing is implemented for classes, structs,
  fields, methods, base relationships, member types, and operation-signature
  types. A user selects only the source
  root: the importer recursively discovers C++ files, infers common include
  roots, tolerates missing external dependencies, and avoids build or vendored
  trees. If a compilation database is present, it is discovered and used
  automatically for higher accuracy. A persisted, validated interface-name
  regular expression classifies matching bases as UML realizations and other
  bases as generalizations. System declarations are excluded and repeated
  header discoveries are deduplicated by Clang symbol identity.
- A shared asynchronous GUI/headless service implements change-set preview,
  persistent source bindings and provenance, last-imported baselines, and
  user-authoritative conflict handling. GUI apply is one undoable command;
  `cpp-preview` and `cpp-import` expose the same rules headlessly. Imported
  relationships are semantic model data and gain diagram connectors only
  where both endpoint presentations exist. Conflicts are structured log entries
  and never overwrite manual model edits.
- C++ namespaces are materialized as source-bound UML package elements.
  Imported types reference those packages through stable IDs, including nested
  namespaces. Package assignment participates in the same three-way baseline:
  a manual reassignment is retained, and a simultaneous source namespace change
  is logged as a conflict rather than overwriting the user model.
- Nested C++ records reference their enclosing class or struct through a stable
  semantic ID and produce a source-bound UML containment relationship from
  owner to nested type. The relationship uses conventional circle-plus nesting
  notation and appears on a diagram whenever both endpoint presentations are
  present.
- Member ownership and signature-use relationships are implemented.
  By-value members and configured owning pointer templates produce composition;
  configured shared pointer templates and raw pointer/reference members produce
  aggregation; unknown wrappers remain association; and parameter/return-only
  use produces dependency. Owning and shared pointer templates are editable,
  persisted preferences (defaulting to `std::unique_ptr` and
  `std::shared_ptr`). Reclassification keeps stable bindings and therefore uses
  the normal user-authoritative conflict rules.
- C++ implementation-file declarations receive the project catalog's `local`
  stereotype. The binding tracks only that derived assignment as source-owned:
  moving the declaration to a header removes it, while manual `private`, `api`,
  and custom stereotype assignments remain intact. Existing projects gain the
  three editable definitions through a duplicate-safe schema migration.
- Per-project repeatable synchronization controls are implemented. Applying a
  successful preview persists its source root in the project manifest as part
  of the same compact undo command as semantic changes. **Synchronize C++**
  reruns discovery without another folder prompt, **Change C++ source…**
  reconfigures it through preview, and headless `cpp-preview`/`cpp-import` may
  omit the source argument once a project has one configured.
- Add explicit conflict-resolution choices and rename/move matching in
  subsequent slices.

## Phase 5: scale and hardening

- The schema-migration foundation is implemented. The persisted schema has a
  version independent from the application, and one shared pre-deserialization
  pipeline can migrate the manifest, model, and diagrams documents together.
  Unversioned POC projects migrate to version 1 in memory, an explicit save
  persists the upgrade, unknown fields survive, and malformed or future
  versions produce actionable diagnostics in both GUI and headless workflows.
- Incremental loading/indexing, merge diagnostics, and expanded recovery
  testing remain.
- Performance benchmarks and rendered-image regression coverage.
- Windows CI and packaging automation is implemented. Every main-branch update
  and pull request performs a clean MSVC Release build with Qt and libclang,
  runs the test suite, and publishes a verified portable ZIP and Qt Installer
  Framework offline installer plus SHA-256 checksums. A matching `v*` tag
  promotes those same artifacts to a GitHub Release. Cross-platform packaging
  remains future work.

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
