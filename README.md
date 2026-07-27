# uuml

`uuml` is a local-first UML and software-architecture modeling tool built with
Qt 6, QML, and C++20.

The MVP includes:

- a semantic model shared by multiple class diagrams;
- a retained C++ Qt Quick scene-graph canvas with batched geometry, deduplicated
  text atlases, zoom-based detail levels, selection, move, resize, pan, zoom,
  connector reconnection, and in-place text editing;
- horizontal diagram tabs that can be detached into standalone tabbed windows
  and arranged across multiple monitors;
- resizable and collapsible project-tree and property panels;
- an error-triggered structured log pop-up;
- undo and redo for model and presentation mutations;
- automatic connector presentation when both relationship endpoints are placed,
  with edge attachment, draggable perimeter ports, and persisted editable bend
  points;
- deterministic, directory-based JSON5 persistence with validation, unknown-field
  retention, and interrupted-save recovery;
- headless `validate`, `cpp-preview`, and `cpp-import` commands using the same
  core services as the GUI.

The accepted boundary and acceptance criteria are in
[`docs/mvp-scope.md`](docs/mvp-scope.md). The broader product direction is in
[`docs/product-architecture-brainstorm.md`](docs/product-architecture-brainstorm.md).
For a step-by-step release audit, use
[`docs/mvp-acceptance-checklist.md`](docs/mvp-acceptance-checklist.md).

## Build on Windows

The primary Windows build uses MSVC 2022 and the matching Qt 6.11 kit. Adjust
the Qt path if your kit is elsewhere.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

The Windows build automatically copies the required Qt runtime, QML modules,
and plugins beside `build/Debug/uuml.exe`. The executable can therefore be
started later from a fresh terminal without setting `PATH` again.

C++ import uses libclang when an LLVM installation is detected. CMake searches
`LLVM_ROOT`, `LLVM_HOME`, and the standard `C:\Program Files\LLVM` location. It
copies `libclang.dll` beside the executable. Builds without an LLVM SDK remain
usable, but C++ import reports that the feature is unavailable.

### Release build

Configure a fresh `build-release` directory, then build and test it:

```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure --timeout 30
```

This Release configuration uses MSVC and the matching Qt 6.11.1 MSVC kit. The
Visual Studio generator locates the MSVC toolchain without requiring a Developer
PowerShell window.

### GitHub builds and releases

GitHub Actions performs the same clean MSVC Release build and complete test
suite for pull requests and changes to `main`. Successful runs provide a
portable Windows ZIP and a conventional Windows installer in the run's
**Artifacts** section. Pushing a version tag such as `v0.1.0` publishes both
tested distributions and their SHA-256 checksums as a GitHub Release.

The package includes the Qt/QML runtime and `libclang.dll`, and is smoke-tested
after deployment. The installer also provides clean removal, Start-menu
integration, `.uuml` file association, and an optional desktop shortcut. See
[`docs/releasing.md`](docs/releasing.md) for downloading development artifacts,
creating releases, version rules, and local packaging.

## Run

Open a new unsaved project:

```powershell
.\build\Debug\uuml.exe
```

Open the included two-diagram example:

```powershell
.\build\Debug\uuml.exe .\examples\sample.uuml
```

Open the generated performance example containing 600 nodes and 1,150
connectors on one diagram:

```powershell
.\build\Debug\uuml.exe .\examples\performance.uuml
```

Regenerate it with the default 600 nodes:

```powershell
cmake --build build --config Debug --target generate_performance_example
```

Validate a project without opening the GUI:

```powershell
.\build\Debug\uuml.exe validate .\examples\sample.uuml
```

Preview or apply C++ imports without opening the GUI:

```powershell
.\build-release\Release\uuml.exe cpp-preview `
  .\examples\sample.uuml C:\path\to\cpp-project
.\build-release\Release\uuml.exe cpp-import `
  .\examples\sample.uuml C:\path\to\cpp-project
```

Select the C++ source root; no configure or build step is required. uuml scans
common C++ source and header extensions recursively and infers the source root
plus common `src` and `include` include paths. When a `compile_commands.json` is
available in or below the selected folder, uuml uses it automatically for more
accurate compiler flags instead. `cpp-import` saves non-conflicting changes and
returns exit code `3` when conflicts need attention; user-edited model content
is never overwritten.

## C++ import

- Choose **File > Import C++…**, then select the project's source folder. That
  is the complete normal workflow: CMake files, a configured build, and a
  compilation database are optional. Discovery runs outside the UI thread and
  presents every create, update, conflict, unchanged, user-owned, or
  missing-source result before import.
- Folder-only discovery parses implementation files first and then standalone
  headers not already reached through them. Build outputs, version-control
  metadata, vendored dependencies, and directory symlink loops are skipped.
  Missing external includes are reported as warnings while usable declarations
  are still imported. If a compilation database is found automatically, the
  preview identifies that higher-accuracy mode instead.
- Import currently covers class and struct names, fields, methods, and direct
  base relationships. Imported types, generalizations, and realizations enter
  the semantic project model without being placed automatically on a diagram;
  double-click types in the tree to add presentations where needed. A connector
  appears automatically once both endpoints of an imported relationship are
  present on a diagram.
- **Preferences > General > C++ import > Interface pattern** controls whether a
  base relationship is realization/implementation or generalization. The
  regular expression is matched against the unqualified base name. Its default,
  `^I[A-Z].*$`, recognizes conventional names such as `IService`; other base
  names remain generalizations. The preview states the classification reason.
- Each imported element and inheritance stores a Clang-derived source binding,
  provenance, and the last imported source snapshot in extensible JSON5
  metadata.
- New projects pre-populate `local`, `private`, and `api` in their editable
  stereotype catalog. Import assigns `local` to classes and structs whose
  definition originates in a C++ implementation file (`.cpp`, `.cc`, `.cxx`,
  `.c++`, `.ixx`, or `.cppm`). That derived assignment is source-owned and is
  removed if the declaration later moves to a header; every other manual
  stereotype assignment remains untouched.
- If only source changed, the next import updates the element. If only the model
  changed, the model stays authoritative. If both changed differently, the
  model is retained and a structured conflict is shown in the log panel.
- Applying a GUI preview is one undoable command. The preview is re-planned
  against current model state immediately before application. Source files are
  read-only and are never rewritten by this workflow.
- Source declarations or inheritance edges that disappear are reported as
  missing but retained in the model. This deliberately avoids destructive
  changes before rename/move matching and explicit conflict resolution exist.

## Diagram interaction

- Use Ctrl-click or Shift-click in the project tree to select multiple model
  types, then drag any selected row onto a diagram. The types are placed as a
  grid at the drop point in one undoable action; relationships appear when both
  of their endpoint types have become present. Types already on that diagram
  are skipped.
- Drag empty diagram space to select every intersecting element. Hold `Shift`
  to add to the current selection or `Ctrl` to toggle intersecting elements.
- Right-click empty space, an element, a connector, or a diagram tab for the
  commands that apply to that exact target.
- Drag any selected element to move the complete selection as one undoable
  command. Press `Delete` to remove selected presentations from the diagram;
  deleting a selected connector removes its relationship.
- Right-click a multi-selection and use **Arrange** to align elements, match
  their size, or distribute three or more elements evenly. The displayed
  shortcuts are local to that diagram window. Distribution uses the smallest
  positive gap already present in the selection. If all elements overlap or
  touch, change the fallback spacing under **Edit > Preferences** (10 px by
  default).
- Use an arrow key to nudge the selection by one diagram unit, or hold `Shift`
  to nudge by ten. Every nudge and arrangement action is independently
  undoable.
- Dragged elements snap to the configured grid and to the edges and centers of
  other elements. Alignment matches display as live guide lines, and a
  multi-selection keeps its internal layout while snapping as one unit. Hold
  `Alt` during a drag to temporarily suppress all snapping. Grid spacing and
  both snapping modes are configured under **Edit > Preferences > General**.
- Open **Edit > Preferences** to change general settings or edit the semantic
  color palette. The Colors page groups roles in a scrollable grid and supports
  both a color picker and hexadecimal values. Changes are applied on **OK**,
  persisted for the application, and refreshed across all diagram windows.
- Use **File > Open Recent…** to reopen one of the ten most recently opened
  projects. Successful opens move a project to the top; failed opens are not
  added. The submenu also provides an action to clear the history.
- Right-click a connector segment and choose `Add bend point here`, or
  double-click a segment. Drag a selected bend handle to shape the route;
  `Delete` removes the selected bend before it removes the relationship.
- Select a connector and drag either square endpoint handle. The end follows
  the pointer while detached and snaps to the exact perimeter position of any
  element under it, including the other endpoint for a self-connection. Release
  to commit one undoable change; press `Escape` or release over empty space to
  retain the original connection. Dragging along the current element edge only
  moves its persisted port.
- Right-click a connector and choose **Routing > Straight** or **Orthogonal**.
  Orthogonal routes automatically maintain 90-degree bends as their elements
  move or resize; manual bend handles remain editable. New relationships use
  the shape selected under **Edit > Preferences > Connectors** (Straight by
  default), while existing relationships retain their own saved setting.
- With two elements selected, use **Create relationship** to create a
  dependency, realization, generalization, navigable association, aggregation,
  or composition. Selection order defines source then target; aggregation and
  composition place their hollow or filled diamond on the source (whole) end.
- To draw directly, hold the left pointer button on an element edge, press
  `D`, `I`, `H`, `A`, `G`, or `C`, drag, and release over the target element.
  The original press and final drop positions become the connector ports. A
  connection may return to its originating element. Press `Escape` or release
  over empty space to discard the candidate. Edit the six unique keys under
  **Edit > Preferences > Connectors**.
- Use `Ctrl+0` to fit the active diagram. Element creation shortcuts are
  `Ctrl+Shift+P/C/S/E`; relationship shortcuts are `Ctrl+Alt+D/I/G/A/C`, with
  `Ctrl+Alt+Shift+G` for aggregation.

Project directories contain `manifest.json5`, `model/model.json5`, and
`diagrams/diagrams.json5`. Strict JSON is accepted because it is a valid JSON5
subset; the loader additionally accepts comments, trailing commas, single-quoted
strings, and unquoted keys. Newly written files omit quotes from identifier-safe
keys while retaining them for unusual or forward-compatible keys that require
quoting. To avoid silently destroying hand-written comments, the MVP reads and
validates commented files but refuses to rewrite them.
