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
- a headless `validate` command using the same core as the GUI.

The accepted boundary and acceptance criteria are in
[`docs/mvp-scope.md`](docs/mvp-scope.md). The broader product direction is in
[`docs/product-architecture-brainstorm.md`](docs/product-architecture-brainstorm.md).
For a step-by-step release audit, use
[`docs/mvp-acceptance-checklist.md`](docs/mvp-acceptance-checklist.md).

## Build on Windows

The commands below use the Qt 6.11 MinGW kit installed by the Qt online installer.
Adjust the two Qt paths if your kit is elsewhere.

```powershell
$env:Path = 'C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.11.1\mingw_64\bin;' + $env:Path
cmake -S . -B build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The Windows build automatically copies the required Qt runtime, QML modules,
plugins, and MinGW runtime beside `build/uuml.exe`. The executable can therefore
be started later from a fresh terminal without setting `PATH` again.

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

## Run

Open a new unsaved project:

```powershell
.\build\uuml.exe
```

Open the included two-diagram example:

```powershell
.\build\uuml.exe .\examples\sample.uuml
```

Open the generated performance example containing 600 nodes and 1,150
connectors on one diagram:

```powershell
.\build\uuml.exe .\examples\performance.uuml
```

Regenerate it with the default 600 nodes:

```powershell
cmake --build build --target generate_performance_example
```

Validate a project without opening the GUI:

```powershell
.\build\uuml.exe validate .\examples\sample.uuml
```

## Diagram interaction

- Drag empty diagram space to select every intersecting element. Hold `Shift`
  to add to the current selection or `Ctrl` to toggle intersecting elements.
- Right-click empty space, an element, a connector, or a diagram tab for the
  commands that apply to that exact target.
- Drag any selected element to move the complete selection as one undoable
  command. Press `Delete` to remove selected presentations from the diagram;
  deleting a selected connector removes its relationship.
- Right-click a connector segment and choose `Add bend point here`, or
  double-click a segment. Drag a selected bend handle to shape the route;
  `Delete` removes the selected bend before it removes the relationship.
- Use `Ctrl+0` to fit the active diagram. Element creation shortcuts are
  `Ctrl+Shift+P/C/S/E`; relationship shortcuts are `Ctrl+Alt+D/G/A`.

Project directories contain `manifest.json5`, `model/model.json5`, and
`diagrams/diagrams.json5`. Strict JSON is accepted because it is a valid JSON5
subset; the loader additionally accepts comments, trailing commas, single-quoted
strings, and unquoted keys. To avoid silently destroying hand-written comments,
the MVP reads and validates commented files but refuses to rewrite them.
