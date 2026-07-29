# yauml Productization Plan

Status: command foundation, the Phase 3 core, contextual toolboxes,
relationship annotations, and stereotypes are implemented. Phase 4 C++ import
and synchronization are implemented. Phase 5 release, scale, and persistence
hardening is underway.

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
- Canvas and tab diagram menus now share one component, so filtering, element
  creation, diagram compartment defaults, and fit-to-diagram remain identical.
  Tab-originated element creation uses the visible viewport center because a
  tab has no scene pointer position. Double-clicking a tab selects the diagram
  in the property inspector and expands that panel when necessary.
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
  resizing is available from all four corner handles.
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
- Diagram-level attribute and operation visibility defaults are implemented,
  with independent presentation-level inherit/show/hide overrides. The
  effective visibility drives scene-graph rendering, in-place hit testing, and
  fit-to-content sizing; all settings persist in diagram JSON5 and use compact
  undo commands.
- Persisted, presentation-only diagram filters are implemented. An always
  visible diagram badge opens criteria for classifier kind, project
  stereotypes, name wildcard, and operation/field wildcard; name and member
  matches can either be included or excluded. Multiple criteria combine, an
  included stereotype set uses any-match semantics, and excluded stereotypes
  take precedence. Hidden presentations remain in project data, their
  connectors are hidden, container frames retain hierarchy context, and
  selection/arrangement operate only on visible items.
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
- Selected connector groups can reattach common ends to any side or shift them
  over snap points as one undoable operation. Reattachment orders remote
  endpoints by their angle relative to the chosen side's outward normal, so
  both tangential and normal placement contribute to a compact, deterministic
  fan. Distance and stable identity only resolve exact angular ties.
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
- Incremental project-tree search is implemented above the browser. It matches
  labels and qualified names case-insensitively, supports `*` and `?`
  wildcards, retains ancestor context, and leaves container drag semantics
  based on the complete unfiltered subtree. `Ctrl+F` focuses the search and
  `Escape` clears it.
- The project tree has persistent configurable columns. In addition to the
  required name column, users can independently show the relative C++ source
  directory, file name, stereotypes, model type, and fully qualified name.
  Columns are selected from either the button beside search or the header
  context menu, and user-resized widths are persisted.
- Custom browser folders are persisted as project data and can be created at
  the model root or inside namespaces, types, and other folders. Native tree
  drag/drop reorganizes selected elements or folders with cycle protection;
  create, rename, move, and delete are compact undoable commands. Deleting a
  folder promotes its direct contents instead of deleting semantic model data.
  Delete is available from the tree context menu and applies atomically to the
  complete extended selection. Deleting a class or struct cascades through its
  semantic nested types, their relationships, and all affected presentations
  as one undoable operation. Persisted cross-type sibling ordering can be
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

### 3.8 Contextual hover toolboxes — implemented

- Keep contextual toolboxes task-specific instead of building one universal
  floating toolbar. The implemented selected-edge toolbox remains dedicated to
  starting relationships. Separate providers cover a selected node or
  container, a multi-selection, and a selected connector, each exposing only
  commands applicable to that target and selection state.
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
- The selected-connector expansion is implemented. Hovering a selected route or
  one of its annotations exposes straight/orthogonal routing, direct in-place
  editors for the name, both roles, both cardinalities, and stereotypes, plus
  reset of all manually positioned annotations. Empty optional annotations can
  be created from the toolbox because the canvas derives their ordinary
  automatic placement before opening the editor. During its show delay the
  toolbox follows the hovered route point, then latches there while the user
  crosses its hover bridge; long relationships therefore do not require a trip
  back to their midpoint.
- Connectors support Ctrl-click and lasso multi-selection. Rectangle
  presentations retain lasso priority: routed lines are considered only when
  the lasso intersects no visible node rectangle. Shift adds and Ctrl toggles
  either kind of selection. Straight/orthogonal routing applies to the complete
  connector selection as one field-sized undo command that records only the
  affected connector modes; precise endpoint, bend-point, and annotation
  editing remains scoped to one active connector.
- The selected-node/container expansion is implemented. Hovering a selected
  node body or selected container header exposes direct name editing, fit to
  content, a lightweight named-style menu with access to style management, and
  wrapping in the direct parent namespace when applicable. The provider keeps
  relationship edges and multi-selection arrangement at higher priority, and
  container bodies do not claim hover from their children. Destructive and
  modal commands remain in context menus until usage proves that placing them
  in a hover surface is beneficial.
- A selected type additionally exposes connector snap-point editing and compact
  `A`/`O` compartment controls. Each control cycles inherited, explicitly
  shown, and explicitly hidden state; faded, pressed, and released visuals make
  the current state visible without expanding the toolbox into six buttons.
- A selected type also exposes directional model expansion: add every missing
  type that depends on it, or every missing type on which it depends. Both
  actions reuse semantic relationship direction, skip packages and existing
  presentations, place the new neighborhood beside the selected type, and
  materialize all newly complete connectors in one undoable command.
- Use one shared visibility state machine: the toolbox appears only for the
  currently selected target, remains open while the pointer crosses a
  hover-safe bridge to the toolbox, stays open during a drag gesture, and
  dismisses on Escape, selection change, diagram deactivation, or a short
  pointer-leave delay. Keyboard focus and touch/pen invocation must not depend
  on hover.
- Toolbox customization is implemented as an application preference. Separate
  ordered lists cover relationship creation, multi-selection, selected
  connectors, and selected elements or containers. Users can enable, disable,
  reorder, and reset non-destructive actions without changing provider priority
  or target applicability. Changes apply immediately to main and detached
  diagram windows; an all-disabled or inapplicable provider does not show an
  empty floating frame. Persistence, accessibility labels, runtime QML
  creation, and existing command undo/redo paths are covered by tests.

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

### 3.10 Automatic layout and obstacle-aware connector routing — planned

- Reconsider automatic element arrangement and automatic connector routing as
  a dedicated diagram-productivity tranche. Keep this separate from the
  already implemented manual alignment/distribution tools and basic Manhattan
  connector routing.
- Start with a short technical and UX evaluation of candidate layout engines
  such as ELK and Graphviz. Compare deterministic output, container and nested
  package support, pinned-node constraints, licensing, deployment size, and
  the ability to preserve user-authored geometry.
- Offer layout for the current diagram or selected container scope, with an
  explicit preview before commit. Hidden filtered items, nested containers,
  pinned presentations, and manually positioned ports must have defined,
  visible behavior rather than being silently moved.
- Commit an accepted layout as one compact undoable command. Cancelling the
  preview must leave the project unchanged, and repeated layout of unchanged
  input must produce the same geometry.
- Extend orthogonal routing with obstacle avoidance only after the layout
  contract is settled. Automatic rerouting must preserve semantic endpoints,
  snap-point attachment, connector annotations, and manual routes unless the
  user explicitly includes those routes.
- Validate the chosen approach on real large C++ diagrams before making it the
  default. Automatic layout and routing remain opt-in until their results are
  predictable enough not to damage carefully authored views.

## Phase 4: C++ import and synchronization — complete

- Multi-folder-first libclang AST indexing is implemented for classes, structs,
  fields, methods, base relationships, member types, and operation-signature
  types. A user selects one or more source folders: the Windows picker supports
  standard multi-selection, overlapping parent/child roots are deduplicated,
  and unrelated sibling roots remain independently scoped. The importer
  recursively discovers C++ files, infers common include roots, tolerates
  missing external dependencies, and avoids build or vendored trees. If a
  compilation database is present for a selected root, it is discovered and
  used automatically for higher accuracy. A persisted, validated interface-name
  regular expression classifies matching bases as UML realizations and other
  bases as generalizations. System declarations are excluded and repeated
  header discoveries are deduplicated by Clang symbol identity.
- A shared asynchronous GUI/headless service implements change-set preview,
  persistent source bindings and provenance, last-imported baselines, and
  user-authoritative conflict handling. GUI apply is one undoable command;
  `yauml-cli cpp-preview` and `yauml-cli cpp-import` expose the same rules
  headlessly. Imported
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
  use produces dependency. Wrapper and container mappings are editable in a
  dedicated **C++ Relationships** preferences page, including relationship
  type, source-end multiplicity, and target template argument. The table has
  duplicate validation and a restore-defaults action. Reclassification keeps
  stable bindings and therefore uses the normal user-authoritative conflict
  rules.
- C++ implementation-file declarations receive the project catalog's `local`
  stereotype. The binding tracks only that derived assignment as source-owned:
  moving the declaration to a header removes it, while manual `private`, `api`,
  and custom stereotype assignments remain intact. Existing projects gain the
  three editable definitions through a duplicate-safe schema migration.
- Per-project repeatable synchronization controls are implemented. Applying a
  successful preview persists its source-root list in the project manifest as
  part of the same compact undo command as semantic changes. **Synchronize C++**
  reruns discovery without another folder prompt, **Change C++ sources…**
  reconfigures it through preview, and headless `yauml-cli cpp-preview` /
  `yauml-cli cpp-import` may omit source arguments once a project has roots
  configured. Older manifests
  containing the singular `sourceRoot` key load into the new list form.
- Removing a configured source folder produces explicit `out-of-scope` preview
  items rather than silently retaining or deleting them. The user can remove
  each item with its relationships and presentations, or detach its source
  binding and keep it as manual model data. Applying is blocked until every
  such item has a decision; cleanup and root changes form one undoable GUI
  transaction. Headless import exposes the same policy through
  `--out-of-scope=remove|keep-manual`.
- A previously imported binding that is not rediscovered inside the active
  roots is shown separately as **NOT FOUND IN SCAN**. Because an incomplete
  best-effort scan can produce this state even when a file still exists, the
  safe default is **Keep for now**. Each row can instead be removed with its
  relationships and presentations or detached as manual model data. Text and
  status filtering can scope the three **visible** bulk actions—for example,
  filtering `uuml::` removes obsolete identities after the `yauml` namespace
  migration without touching unrelated rows. Headless preview/import provides
  the equivalent `--missing-source=keep|remove|keep-manual` policy.
- Long-running GUI discovery now exposes live phase, current-file, and
  determinate translation-unit progress through the shared import service. The
  headless workflow uses the same service but may omit the optional progress
  observer; import planning and conflict semantics remain identical.
- Explicit per-conflict resolution is implemented in the GUI preview.
  **Unresolved** preserves the previous safe behavior, **Keep model**
  acknowledges the current source baseline without replacing user fields, and
  **Use C++ source** applies the complete source-owned candidate. Bulk choices
  affect only conflicts with complete, unambiguous candidates; malformed
  duplicate bindings and unresolved relationship endpoints remain manual.
  Resolved changes join ordinary import changes in one compact undo command.
  Headless preview/import exposes the same bulk policies through an explicit
  `--conflicts` option and retains exit code 3 while any unsafe or unselected
  conflict remains unresolved.
- The GUI preview presents Status, Item, Type, Source, and Resolution as
  separate columns. Every header sorts in both directions; a status selector
  includes a focused **Needs decision** view, and free-text filtering covers
  names, types, source paths, messages, classification details, and
  resolutions. Filtering and sorting operate only on the view, so per-item
  conflict, not-found, and out-of-scope decisions remain keyed to stable model
  subjects. **Set filtered…** applies a conflict or cleanup resolution to the
  current status/text-filter result, and both individual and bulk decisions
  preserve the list's scroll position while the preview refreshes.
- Conservative rename/move matching is implemented for imported declarations.
  It compares only unmatched source declarations with their previous import
  baselines and requires a unique mutual-best match supported by at least two
  independent evidence categories: source location, language-level name, and
  member structure. One-to-one descendant evidence carries renamed namespace
  package identity, while endpoint and evidence matching carries relationship
  identity. Model IDs therefore remain stable for diagrams, styles, folders,
  annotations, and connector presentations. Ambiguous matches remain explicit
  new/missing records and are never guessed. All matched updates still use the
  normal three-way user-authority and conflict-resolution rules.

## Phase 5: scale and hardening

- The schema-migration foundation is implemented. The persisted schema has a
  version independent from the application, and one shared pre-deserialization
  pipeline can migrate the manifest, model, and diagrams documents together.
  Unversioned POC projects migrate to version 1 in memory, an explicit save
  persists the upgrade, unknown fields survive, and malformed or future
  versions produce actionable diagnostics in both GUI and headless workflows.
- Interrupted multi-file save recovery is implemented and covered by a
  controlled corruption test. External modification protection is also
  implemented: each open session retains byte-level file revisions, saving
  refuses to overwrite changed files, and the GUI offers Save As, reload,
  cancel, or explicit overwrite. Headless C++ import shares the same guard and
  has a distinct conflict exit code plus an explicit overwrite option.
- Rendered-image regression coverage is implemented against the production
  Qt Quick RHI path with fixed scale, font, palette, viewport, and a reviewed
  platform-specific baseline. The fixture covers packages, type compartments,
  stereotypes, relationship annotations, routing, decorations, and the grid;
  local and CI failures retain expected, actual, and amplified difference
  images. Comparison allows bounded per-pixel glyph antialiasing differences
  between Windows/Qt releases while a stricter whole-image mean still rejects
  geometry, routing, or palette movement. The harness counter-scales the scene
  by the actual window device-pixel ratio and compares a fixed physical
  viewport, keeping the reference stable on 100%, 125%, and mixed-DPI setups.
- Recovery fault coverage includes missing and malformed backup files plus
  deterministic failures at every atomic open, write, and commit boundary.
  The test matrix covers all three recovery backups, the pending marker, all
  three live project files, and all three restore targets. It includes genuine
  mixed generations and verifies that a failed restore retains the complete
  backup set for a later retry. The injection seam is scoped and thread-local,
  so it exercises the production persistence path without leaking into other
  tests or changing application behavior.
- Incremental loading/indexing and synthetic performance benchmarks are
  intentionally last in this phase. The current scene-graph implementation is
  performing well on the product owner's large real project, so architectural
  complexity will be introduced only when observed behavior justifies it.
- Windows CI, packaging, and stable updates are implemented. Every main-branch
  update and pull request performs a clean MSVC Release build with Qt and
  libclang, runs the test suite, and publishes a verified portable ZIP and Qt
  Installer Framework hybrid installer plus SHA-256 checksums. A matching `v*`
  tag promotes those same artifacts to a GitHub Release and publishes the
  corresponding IFW repository through GitHub Pages. Installed builds check
  for stable updates at a configurable daily interval and hand user-approved
  updates to the maintenance tool after the normal save-and-close flow.
  Cross-platform packaging remains future work.
- Windows distribution separates the GUI-subsystem `yauml.exe` from the
  console-subsystem `yauml-cli.exe`. This keeps normal desktop startup free of
  a terminal window while preserving script-friendly validation and C++ import;
  packaging verifies both PE subsystem values to prevent regression.

## Phase 6: diagram export — in progress

- Full-diagram PNG export is implemented through an off-screen instance of the
  production Qt Quick canvas. It exports the complete filtered diagram rather
  than the visible viewport, preserves styles, text, connector annotations, and
  container clipping, and omits editor-only grid and interaction overlays.
- Export dimensions are bounded to avoid accidental excessive allocations;
  failures and successful destinations are reported through the application
  diagnostics. A rendered-output regression covers content beyond the viewport.
- Add PDF export through the same export orchestration and diagram renderer.
- Add SVG export only when there is concrete product demand; it remains outside
  the critical productization path.

## Release gates for every tranche

- Human-maintainable implementation with cohesive responsibilities and comments
  for invariants and non-obvious tradeoffs.
- Focused command/undo/persistence tests for new model state.
- Successful MSVC Release build and complete CTest pass.
- Checklist or manual scenario for behavior that cannot yet be automated.
