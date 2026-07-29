# yauml MVP Scope

> Historical document. This scope was accepted for the completed MVP and is
> retained only as a record of the original product boundary. For the current
> product, see the [README](../../README.md) and
> [productization plan](../productization-plan.md).

Status: archived accepted baseline

## 1. MVP objective

Deliver a local-first desktop modeling tool for creating and maintaining a
small, Git-friendly semantic model and multiple class diagrams. The MVP proves the
domain model, transaction boundary, persistence format, diagram interaction,
and shared GUI/headless architecture without depending on C++ source
synchronization.

SVG, PNG, and PDF export are outside the MVP. A saved project is the MVP's
portable output.

## 2. Included semantic model

- Projects and packages.
- Classes, structs, and enumerations.
- Attributes, operations, parameters, visibility, and common modifiers.
- Enumeration literals.
- Generalization, dependency, and association relationships.
- Stable immutable IDs for every semantic and presentation object.
- Validation diagnostics for invalid ownership, references, types, and schema
  versions.

Interfaces, aggregation, composition, advanced language traits, and a broader
UML metamodel may be added after the vertical slice if they do not jeopardize
the MVP schedule.

## 3. Included diagram behavior

- Multiple class diagrams per project.
- The same semantic element can appear on multiple diagrams. Each presentation
  has independent geometry and visual state while sharing semantic properties.
- Selection and multi-selection.
- Move and resize.
- Pan, zoom, and fit to content.
- Straight connectors with automatic edge attachment and draggable, persisted
  perimeter ports stored as an edge plus normalized offset.
- Basic connector selection and reconnection.
- Every editable text value rendered on the diagram can be edited in place.
  This includes classifier names, attribute and operation text, enumeration
  literals, and editable relationship labels.
- Undo and redo for every mutating action.
- A model browser, property inspector, tabbed diagram area, command history, and
  log panel.
- Basic keyboard access for commands and navigable non-canvas controls.

Orthogonal routing, editable bend points, alignment guides, distribution tools,
stylesheets, automatic layout, and persisted workspace restoration are post-MVP
capabilities.

In-place text editing is a first-class command interaction, not a shortcut that
bypasses the domain model. Starting an edit preserves the original value;
committing produces one validated undo entry, and cancelling restores the value
without changing history. The property inspector remains an alternative editor
for the same properties and uses the same validation and command path.

## 4. Workspace and window layout

The main window has three primary areas:

- A central diagram area with a horizontal tab bar. Every project diagram is
  represented by exactly one tab hosted in either this area or a standalone
  diagram-area window.
- A resizable, collapsible panel on the left containing the project tree.
- A resizable, collapsible panel on the right containing vertically arranged or
  otherwise navigable subpanels. The first required subpanel is **Selected
  element properties**; the area is designed to accept additional subpanels.

The structured log is a pop-up panel rather than a permanently occupied main
column. A newly reported error opens the log panel automatically. Opening the
panel must not discard the user's current diagram selection or in-progress work.

Diagram tabs support detachable tab groups:

1. Dragging a diagram tab out of its tab bar creates a standalone diagram-area
   window containing that diagram.
2. A standalone diagram-area window has its own horizontal tab bar and can
   receive other diagrams dragged from the main window or another standalone
   window.
3. A diagram can be dragged back to the main window or moved between standalone
   windows without creating or losing semantic or presentation objects.
4. All windows share one project session, command/transaction system, and saved
   model. The main-window property panel follows the selection in the currently
   focused diagram, including diagrams hosted in standalone windows.
5. Standalone windows can be positioned independently across several monitors so
   multiple diagrams remain visible and interactive at the same time.
6. Closing a standalone diagram-area window safely returns all of its tabs to the
   main diagram area.

Persisting panel sizes, tab placement, detached-window geometry, and monitor
assignment across application restarts remains post-MVP. The complete behavior
above is required within a running session.

## 5. Persistence

- Projects use directory-based JSON5 files.
- Semantic data and diagram presentation data remain separate.
- Files contain an explicit schema version and stable IDs.
- The writer produces deterministic field and collection ordering.
- Saving an unchanged project produces no diff.
- Writes are recoverable after interruption; the implementation must not assume
  that replacing several files is one atomic operation.
- Unknown fields and comments must never be silently discarded. The JSON5
  parser/writer prototype must establish whether they can be retained safely or
  must cause a clear refusal to rewrite the affected file.

The initial project partitioning strategy may be simple. Package-based or
user-controlled partitioning is not required until there is evidence that a
single semantic file creates unacceptable merge behavior.

## 6. Editing and authority

All mutations pass through the same C++ command and transaction layer, whether
they originate in the GUI or the headless tool.

User edits are authoritative. When later source-import or synchronization work
proposes a different value for a user-edited property:

1. Keep the user value.
2. Do not silently overwrite it.
3. Record a structured conflict in the log.
4. Retain enough context to identify the affected element, property, user value,
   and proposed source value.

The model therefore needs property-level provenance or an equivalent ownership
mechanism; object-level `generated` flags are not sufficient.

## 7. Log panel

The application includes a structured log panel from the MVP. It presents:

- validation errors and warnings;
- load, save, migration, and recovery events;
- command failures and rollback information;
- future import and source/manual conflicts;
- severity, category, message, and links to affected model elements where
  available.

Whether logs persist across application sessions is a separate design decision.
Logs are diagnostics and must not become a second source-control or merge system.

## 8. Headless architecture

A headless `yauml-cli` executable is part of the architecture from day one. It
uses the same domain, validation, persistence, transaction, and diagnostic
services as the `yauml` desktop application.

The MVP must provide a functional command equivalent to:

```text
yauml-cli validate <project>
```

The CLI and core also define the import/change-set boundary needed by a future
command equivalent to:

```text
yauml import <project> <input>
```

Concrete C++ AST importing and repeated source synchronization are not MVP exit
criteria. Import implementations must propose a change set and report structured
diagnostics rather than mutating model files directly.

## 9. Explicitly outside the MVP

- C++ parsing and source synchronization.
- Source rename and move matching.
- SVG, PNG, and PDF export.
- Diagram types other than a class diagram.
- Complete UML 2.x coverage and all behavioral diagrams.
- Automatic layout and advanced connector routing.
- Project stylesheets and diagram-local style overrides.
- Real-time collaboration, hosted storage, or required accounts.
- Languages other than C++.
- Cross-platform release packaging; Windows is the MVP delivery platform.

Cross-platform compatibility remains an architectural constraint even though
only Windows packaging is required.

## 10. MVP acceptance criteria

The MVP is complete when all of the following are demonstrable:

1. A user can create, open, edit, save, and reopen a project without semantic or
   diagram data loss.
2. A user can create the included model elements and relationships, create
   multiple class diagrams, and place the same model element on more than one
   diagram with independent presentation geometry.
3. Move, resize, reconnect, property edits, create, and delete operations undo
   and redo to exactly equivalent states.
4. Every editable text value shown on the diagram can be changed in place,
   committed as one undoable action, or cancelled without a model change.
5. Saving the same state twice produces byte-for-byte identical project data.
6. Invalid files and broken references produce actionable diagnostics in the GUI
   log and through `yauml-cli validate`.
7. An interrupted save can recover the last valid project state.
8. The GUI and CLI use the same domain and persistence implementation.
9. The main window provides the specified central tabbed diagram area and
   collapsible, resizable left and right panels.
10. A diagram tab can be detached into a standalone tabbed diagram-area window,
    moved among diagram-area windows, and returned to the main window without
    model or presentation data loss.
11. Multiple diagram-area windows can display and edit diagrams simultaneously
    across multiple monitors while sharing one project session.
12. A new error automatically opens the log pop-up without clearing the current
    selection or losing an in-place edit.
13. Closing a standalone diagram-area window returns its diagrams to the main
    window without closing or deleting them.
14. A diagram remains responsive at a documented MVP reference size established
   by the Phase 0 prototype.

The 2,000-visible-node, 60 FPS target remains a product-scale performance goal,
not an MVP exit criterion until the prototype establishes a representative and
measurable baseline.

## 11. Delivery sequence

### Phase 0: foundations and risk prototypes

- Record the core architectural decisions as ADRs.
- Prototype deterministic, comment-aware JSON5 round trips.
- Prototype the C++ core/QML boundary and custom diagram item.
- Establish a repeatable rendering and interaction benchmark.
- Prototype tab detachment, cross-window diagram moves, and shared selection
  context across multiple top-level windows.
- Create shared structured diagnostics and the GUI log panel shell.
- Create the headless executable with validation service wiring.

### Phase 1: semantic vertical slice

- Implement IDs, the included metamodel, validation, and transactions.
- Implement project creation, JSON5 load/save, recovery, and CLI validation.
- Implement the main-window layout, project tree, selected-element property
  panel, and error-triggered pop-up log.

### Phase 2: diagram vertical slice

- Implement the multi-diagram presentation model, horizontal tabs, and canvases.
- Implement selection, move, resize, connectors, viewport controls, and in-place
  editing for every editable diagram text value.
- Implement tab detachment, standalone tabbed diagram-area windows, cross-window
  tab movement, and shared project/selection context.
- Complete exact undo/redo coverage and MVP acceptance tests.

### Post-MVP

- Expand manual modeling productivity.
- Add persisted workspace restoration, styling, layout, and export.
- Implement C++ import and later repeatable source synchronization using the
  predefined change-set, provenance, authority, and logging boundaries.
