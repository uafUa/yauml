# uuml: Product and Architecture Brainstorm

Status: draft for discussion

## 1. Working vision

`uuml` is a local-first modeling tool for software architecture and detailed
design. It combines precise manual diagram editing with reliable synchronization
from C++ source code. Models are stored in a human-readable, Git-friendly form.

The tool should feel closer to an engineering IDE than to a drawing program:

- semantic model elements exist independently of any diagram;
- the same element can appear on multiple diagrams;
- diagrams preserve carefully authored layout;
- source synchronization updates semantics without destroying manual names,
  relationships, styles, or geometry;
- every model and diagram edit participates in consistent undo/redo;
- storage can be inspected, reviewed, and merged without launching the tool.

The first release is intentionally not a complete implementation of UML. It is
a focused architecture and class-diagram tool that can grow toward broader UML
coverage without redesigning its core.

## 2. Primary users and use cases

### Primary users

- C++ developers documenting an existing codebase.
- Software architects maintaining architecture views alongside source code.
- Reviewers who need diagrams and model changes to be understandable in Git.
- Small teams that want local files rather than a required modeling server.

### Primary workflows

1. Create packages, classes, interfaces, enumerations, and relationships.
2. Place selected model elements on one or more diagrams.
3. Import or synchronize classifiers from a C++ compilation database.
4. Refine diagrams manually without having layout overwritten by synchronization.
5. Rename or move source declarations while preserving model identity.
6. Review semantic and diagram changes as readable source-control diffs.
7. Export diagrams as SVG, PNG, and PDF for documentation.

## 3. Proposed scope

### MVP semantic model

- Projects and packages/namespaces.
- Classes, structs, interfaces, and enumerations.
- Nested classifiers and explicit containment.
- Attributes, operations, parameters, visibility, and common modifiers.
- Enumeration literals.
- Generalization and interface realization.
- Dependency, association, aggregation, and composition.
- Source locations and source identities attached to generated elements.
- User-created and source-generated elements in the same project.
- Validation diagnostics for broken references and unsupported constructs.

### MVP diagram capabilities

- Package and class diagrams.
- Multiple visual presentations of one semantic element.
- Selection, multi-selection, move, resize, pan, zoom, and fit-to-content.
- Orthogonal and straight connectors.
- Add, remove, and move connector bend points.
- Straighten, make horizontal, and make vertical actions.
- Align, equal-size, minimum-size, and distribute actions.
- Snap-to-grid, alignment guides, and configurable spacing.
- Context menus based on the selected object, connector, or bend point.
- Undo/redo for every mutating action, including geometry and connection state.
- Styles based on element type, properties, package ownership, and diagram-local
  overrides.
- Model browser, property inspector, diagram tabs, and persistent workspace state.

### C++ synchronization

- Consume `compile_commands.json` and Clang's AST rather than parsing C++ text
  heuristically.
- Discover declarations from headers and implementation files.
- Handle namespaces, nested classes, PIMPL types, enums, aliases, templates, and
  declaration/definition pairs.
- Respect visibility rules while allowing configurable import policies.
- Preserve stable model identity across source renames and moves.
- Support an explicit rename/identity mapping file for ambiguous cases.
- Present a preview of create, update, move, rename, and delete operations before
  applying synchronization.
- Apply synchronization as one undoable transaction.
- Never modify diagram geometry merely because source semantics changed.

### Explicitly outside the MVP

- Complete UML 2.x coverage.
- Behavioral diagrams such as sequence, activity, and state-machine diagrams.
- SysML.
- Round-trip source-code generation.
- Real-time multi-user editing.
- A hosted project repository or mandatory account.
- Binary plugin ABI stability.
- Imports for languages other than C++.

These are possible later extensions, not constraints the MVP must satisfy.

## 4. Functional requirements

### Model integrity

- Every semantic and presentation object has a stable, immutable ID.
- Names and qualified names are mutable properties, never object identity.
- Relationships are stored canonically once; inverse views are derived.
- Deleting an element reports or handles affected presentations and relations.
- Model loading validates types, references, ownership, and schema version.
- Unknown future fields should be retained when practical or rejected clearly,
  never silently discarded.

### Editing and commands

- All mutations go through a command/transaction layer.
- A user gesture such as a drag or resize produces one undo entry.
- Undo restores the complete semantic and visual state, including connector
  constraints, handles, and endpoint attachments.
- Commands expose human-readable descriptions for history and diagnostics.
- Long-running commands can be cancelled before commit.

### Workspace behavior

- Restore open diagrams, active tab, model-browser expansion and selection,
  viewport positions, zoom, splitter positions, and inspector state.
- Keep workspace state separate from the shared semantic model where appropriate.
- Detect external file changes and offer reload, compare, or keep-local choices.

### Import safety

- Synchronization must distinguish generated properties from user-owned
  properties.
- Removing a source declaration should not immediately destroy user-authored
  diagrams; stale elements should be reviewable before deletion.
- Identity mappings must be visible and editable as project data.
- Re-running synchronization without source changes must produce no model diff.

## 5. Non-functional requirements

The following targets are provisional and should be validated with an early
performance prototype.

- Windows is the first supported platform; Linux and macOS remain architectural
  requirements.
- Maintain 60 FPS while panning or zooming a diagram with 2,000 visible nodes and
  connectors on representative developer hardware.
- Keep pointer-to-visual feedback below one display frame during normal editing.
- Open a 50,000-element semantic model incrementally without freezing the UI.
- Run parsing, layout, validation, and persistence work outside the GUI thread.
- Recover the last valid project after interrupted writes.
- Produce deterministic serialized output for an unchanged model.
- Avoid unnecessary whole-project rewrites.
- Provide keyboard access for commands and navigable non-canvas UI controls.
- Support high-DPI displays and fractional scaling.

## 6. Framework decision

### Decision: Qt 6, QML, and modern C++

Use Qt 6 for the desktop shell and rendering infrastructure, QML for declarative
application UI, and C++20 or newer for the domain and graphics core.

Reasons:

- Qt Quick provides a retained, GPU-accelerated scene graph with direct extension
  points for custom diagram rendering.
- QML is productive for panels, menus, inspectors, tabs, shortcuts, and animation.
- C++ integrates directly with Clang tooling and existing C++ build metadata.
- The model and geometry engine can remain independent of QML and be tested
  without a GUI.
- Qt supports Windows, Linux, and macOS from one application architecture.

Constraints and risks:

- Qt licensing and distribution obligations must be reviewed before product
  commitments.
- Exposing too much mutable C++ state directly to QML would create fragile data
  flow. QML should invoke commands and consume stable view models.
- Custom scene-graph code requires careful render-thread ownership and lifetime
  management.
- QML/C++ integration adds build and debugging complexity compared with a pure
  web application.

## 7. Architectural principles

1. The semantic model is independent from diagrams.
2. Diagram presentation is independent from semantic ownership.
3. QML renders view models; it does not enforce domain invariants.
4. Every mutation is a transaction.
5. Stable IDs survive rename, move, and reformat operations.
6. Source synchronization is a model merge, not a model regeneration.
7. Human-readable storage is a supported interface with a schema and validator.
8. Expensive work uses immutable snapshots and worker threads.
9. Rendering performance is measured with realistic large diagrams from the
   beginning.
10. Features must be usable from commands and shortcuts, not only pointer menus.

## 8. Proposed component architecture

```text
QML application shell
  |-- model browser / search
  |-- diagram tabs and workspace
  |-- property inspector
  |-- menus, commands, history, diagnostics
  |
C++ application/view-model layer
  |-- project session
  |-- QAbstractItemModel adapters
  |-- command registry and selection context
  |-- workspace-state service
  |
C++ domain core
  |-- semantic graph and metamodel
  |-- presentation graph
  |-- command transactions and undo/redo
  |-- validation and indexing
  |-- style resolution
  |
C++ diagram engine
  |-- geometry and constraints
  |-- connector routing
  |-- hit testing and spatial index
  |-- Qt Quick scene-graph renderer
  |-- export renderer
  |
Infrastructure
  |-- project serializer and migrations
  |-- Clang source synchronizer
  |-- automatic layout adapter
  |-- atomic file and recovery service
```

Dependencies should point inward: infrastructure and UI depend on domain
interfaces; the domain core does not depend on QML, Clang, or a particular file
format.

## 9. Domain model sketch

### Semantic objects

- `Project`
- `Package`
- `Classifier`
- `Class`, `Interface`, `Enumeration`
- `Property`, `Operation`, `Parameter`, `EnumerationLiteral`
- `Relationship` and typed relationship specializations
- `SourceBinding`

### Presentation objects

- `Diagram`
- `NodePresentation`
- `ConnectorPresentation`
- `LabelPresentation`
- `EndpointAttachment`
- `BendPoint`
- `StyleOverride`

A presentation references a semantic object but owns its own geometry and visual
settings. Two presentations of one class therefore share semantics but may have
different position, size, compartment visibility, and style overrides.

### Identity

Use immutable UUIDv7 or equivalent opaque IDs internally. Store readable names
alongside them. Source-generated elements additionally carry a source identity:

```yaml
id: 0196f4b8-8d1d-7f53-9f31-8e7082eeb463
name: GrpcInputIngressBridge
qualified_name: hps::adapters::boundary::grpc::GrpcInputIngressBridge
source:
  language: cpp
  declaration: src/adapters/boundary/grpc/grpc_input_ingress_bridge.hpp
  symbol: "clang-usr-or-normalized-symbol-key"
```

The Clang USR or normalized symbol key is evidence for matching, not the sole
permanent model ID. Explicit rename mappings can reconnect an element when source
identity changes.

## 10. Persistence decision draft

### Decision: directory-based project with canonical text files

Proposed structure:

```text
example.uuml/
  manifest.yaml
  model/
    architecture.yaml
    application.yaml
    adapters-grpc.yaml
  diagrams/
    runtime-components.yaml
    grpc-boundary.yaml
  styles/
    default.css
  workspace/
    shared.yaml
```

Per-user ephemeral workspace state should normally live outside the repository,
for example in Qt's platform configuration location. A project may opt into a
small shared workspace file for intentional team defaults.

### Storage rules

- Use a documented strict subset of YAML, or reconsider formatted JSON after a
  parser/round-trip prototype. Human readability is required; YAML itself is not
  yet a final commitment.
- Emit a canonical field and collection order.
- Partition files by package or user-selected model unit to reduce merge conflicts.
- Store each relationship only at its canonical owner.
- Keep diagram geometry out of semantic files.
- Write changed files to temporary siblings, flush, and atomically replace.
- Include an explicit schema version and deterministic migration pipeline.
- Provide `uuml validate` and `uuml format` command-line operations.
- Never require users to understand internal IDs for ordinary edits, while keeping
  IDs visible enough to diagnose merges.

An optional single-file archive can be added for exchange, but the unpacked
directory remains the source-control format.

## 11. Diagram rendering architecture

### Qt Quick integration

Use standard QML controls for the application chrome. Implement the diagram as a
custom `QQuickItem` backed by C++ scene-graph nodes rather than thousands of QML
objects.

The diagram engine should maintain a retained render representation containing
batched geometry for shapes, connectors, selection overlays, and text. It should
update only dirty presentations after a command.

### Interaction

- Keep hit testing in C++ using a spatial index.
- Convert pointer input into explicit interaction states such as selecting,
  dragging, resizing, reconnecting, and moving a bend point.
- Preview gestures without committing domain commands every frame.
- Commit one command transaction at gesture completion.
- Keep selection handles and guides in a lightweight overlay layer.
- Make context-menu content derive from command applicability for the current
  selection or hit target.

### Connector geometry

Represent endpoint attachment separately from endpoint coordinates. This prevents
undo or alignment operations from restoring a point while losing its connection
constraint. Routing operations should produce complete geometry snapshots or
reversible edits, including:

- attached endpoint and port;
- endpoint offset or constraint;
- bend points;
- routing mode;
- horizontal/vertical preferences.

## 12. Undo and transaction architecture

The command layer is part of the domain core, not a QML convenience service.

- Commands validate before mutation.
- A transaction records all semantic and presentation changes.
- Composite actions contain child commands but appear as one history entry.
- Continuous gestures coalesce preview changes into one committed command.
- Import and automatic layout are transactions.
- Undo/redo emits the same model-change notifications as forward execution.
- Failed commands roll back completely.

For simple property changes, reversible deltas may be sufficient. For connector
editing and bulk transformations, use typed before/after snapshots of all affected
geometry and constraints. Do not serialize the complete project for each undo
entry.

## 13. Source synchronization architecture

### Pipeline

1. Locate and load the compilation database.
2. Parse translation units through Clang tooling on worker threads.
3. Normalize AST declarations into a language-neutral source model.
4. Match source declarations against existing `SourceBinding` records.
5. Apply explicit rename mappings.
6. Calculate a change set without modifying the project.
7. Show conflicts and a synchronization preview.
8. Apply accepted changes as one transaction.

### Matching strategy

Use several signals in order:

1. Existing stable source binding or Clang USR.
2. Explicit rename mapping.
3. Declaration location and enclosing symbol.
4. Structural similarity as a suggested match requiring confirmation.
5. Otherwise create a new model element.

The synchronizer must not match solely by display name or qualified name. Both can
legitimately change while the model element and its diagram presentations remain
the same.

## 14. Styling

Use a CSS-inspired selector language, but define it as a product schema rather
than depending on browser CSS behavior.

Candidate selectors:

```css
class {
  background-color: #ffffff;
}

class[owner.qualifiedName="hps::adapters::boundary::grpc"] {
  background-color: #f5f0dc;
}

class[external=true] {
  border-style: dashed;
  opacity: 0.8;
}
```

Resolution order should be deterministic: built-in defaults, project stylesheet,
diagram stylesheet, then presentation-local overrides. Cache resolved styles and
invalidate them by dependency rather than recomputing the full diagram.

## 15. Threading and responsiveness

- The GUI thread owns QML-facing view models and interaction state.
- The render thread owns Qt scene-graph resources according to Qt lifecycle rules.
- Worker threads perform Clang parsing, validation, automatic layout, indexing,
  and serialization from immutable model snapshots.
- Worker results return as proposed change sets and are committed on the model
  thread through transactions.
- Cancellation tokens and progress events are required for long operations.

Avoid fine-grained cross-thread mutation of the live model. Snapshot plus change
set boundaries are easier to reason about and test.

## 16. Build and dependency direction

- CMake with Qt's CMake integration.
- C++20 as an initial baseline; adopt C++23 features selectively where supported by
  target compilers.
- Qt Quick, Qt Quick Controls, Qt SVG, and Qt Test.
- Clang/LLVM tooling for C++ import.
- A small, explicit serialization dependency selected after the YAML-versus-JSON
  prototype.
- An automatic-layout engine behind an adapter; ELK or Graphviz are candidates.
- Avoid exposing third-party types in domain interfaces.

The exact package manager is undecided. Conan and vcpkg should be compared against
Qt deployment, LLVM distribution, CI caching, and Windows developer setup before
selection.

## 17. Testing strategy

- Domain unit tests without Qt GUI dependencies.
- Command tests that verify execute, undo, redo, and rollback equivalence.
- Property-based tests for model graph invariants and serialization round trips.
- Golden-file tests for canonical project serialization and migrations.
- Synchronization fixtures backed by real small CMake projects and compilation
  databases.
- Diagram geometry tests for routing, alignment, distribution, snapping, and
  endpoint constraints.
- QML integration tests for selection, menus, shortcuts, and workspace restore.
- Rendered-image regression tests for a limited set of stable diagrams.
- Performance benchmarks with generated and real large models.
- Crash-recovery tests that interrupt writes at controlled points.

Every connector-editing test should verify the full cycle:

```text
initial state -> command -> expected state -> undo -> exact initial state
              -> redo -> exact expected state
```

## 18. Delivery phases

### Phase 0: risk prototypes

- Render and interact with a representative 2,000-node diagram.
- Prototype C++/QML boundaries and a custom `QQuickItem` renderer.
- Parse a real CMake compilation database with Clang.
- Compare strict YAML and formatted JSON for readable, deterministic round trips.
- Validate Qt licensing and Windows packaging.

### Phase 1: vertical slice

- Project load/save and validation.
- Packages, classes, enums, and core relationships.
- Model browser and property inspector.
- One class diagram with nodes and connectors.
- Transactional editing and undo/redo.
- SVG export.

### Phase 2: productive manual modeling

- Multi-diagram support and workspace restoration.
- Full connector and bend-point editing.
- Alignment, sizing, distribution, guides, and keyboard commands.
- Stylesheets and presentation overrides.
- PNG/PDF export.

### Phase 3: C++ synchronization

- Compilation-database discovery and AST index.
- Source bindings and rename mappings.
- Synchronization preview, conflict handling, and undo.
- Nested classifiers, PIMPL types, enum literals, and implementation-file types.

### Phase 4: scale and hardening

- Incremental loading and indexing.
- Large-diagram virtualization and rendering optimization.
- Schema migrations, crash recovery, and merge diagnostics.
- Cross-platform packaging and release automation.

## 19. Early architectural decisions to record as ADRs

1. Qt/QML/C++ technology baseline.
2. Semantic model separated from presentations.
3. Command transactions as the only mutation API.
4. Custom Qt Quick scene-graph diagram renderer.
5. Directory-based canonical text storage.
6. Stable opaque model IDs separated from source identity.
7. Clang AST synchronization through compilation databases.
8. Snapshot/change-set boundaries for background work.
9. Windows-first, cross-platform architecture.

## 20. Open questions

- Is the product limited to software architecture, or should its metamodel be
  extensible enough for broader UML and SysML from the beginning?
- Should structs and classes remain distinct domain types or one classifier with
  language-specific traits?
- Is strict YAML worth its parser and canonicalization complexity, or is formatted
  JSON the more dependable human-readable contract?
- How should model files be partitioned by default: package, namespace, source
  module, or explicit user choice?
- Should source-generated relationships be editable, overridable, or only
  suppressible?
- How are conflicting source changes and manual edits presented without creating
  a second version-control system inside the application?
- Which layout engine produces acceptable class diagrams while preserving pinned
  nodes and manual routing?
- What is the minimum supported Qt version and corresponding Windows Web-free
  deployment footprint?
- Which Qt license and dependency licenses are acceptable for the intended
  distribution model?
- Should a headless CLI ship from the first release for validate, format, import,
  migrate, and export operations?

## 21. Suggested next step

Do not begin with the full application shell. Build one risk-focused executable
that loads a generated model and demonstrates:

- a custom Qt Quick scene-graph canvas;
- 2,000 nodes plus connectors at interactive frame rates;
- selection, pan, zoom, move, and one undoable bend-point edit;
- model data held entirely in the C++ core;
- deterministic save and reload of the same diagram.

That prototype will answer the largest architecture and performance questions
before the storage schema and public model API become expensive to change.
