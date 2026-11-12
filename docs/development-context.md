# yauml development context

This is the compact, stable engineering orientation for a new Codex session or
developer machine. Read it together with [`current-handover.md`](current-handover.md),
which records the moving state of the latest submitted slice.

## Product in one paragraph

yauml (Yet Another UML) is a Windows-first Qt 6 desktop application for
understanding existing C++ systems through UML. It imports source with
libclang, maintains a shared semantic model, and lets users build multiple
independent class, sequence, and activity diagrams. Projects are readable,
directory-based JSON5 and can be validated or imported headlessly. The current
product emphasizes fast Qt Quick rendering, safe repeated source
synchronization, manual diagram productivity, and documentation export.

The product-facing overview is in [`../README.md`](../README.md). The detailed
capability ledger is [`productization-plan.md`](productization-plan.md).

## Current baseline

- Application version: `0.13.0` in `CMakeLists.txt`.
- Project schema: `13` in `src/core/project_schema_version.h`.
- Primary platform: Windows x64, MSVC 2022, Qt 6.
- C++ baseline: C++20.
- C++ import: libclang is required for accepted Release and CI builds.
- Stable distribution: GitHub Release installer and portable ZIP, plus a Qt
  Installer Framework update repository on GitHub Pages.

Version and schema are independent. Increase the schema only with a migration;
increase the application version according to the release process.

## Architectural invariants

### Semantic model and diagram presentation are separate

Types, packages, operations, and relationships are project semantics. A diagram
contains presentations of those subjects plus layout, routing, filtering, and
display choices. Removing a source-derived connector presentation must not
silently delete its semantic relationship. One semantic element may appear on
many diagrams with different geometry and detail.

### Every committed mutation is a command

User-visible mutations go through concrete polymorphic `ProjectCommand`
objects. Each command owns only the data required for `execute()` and `undo()`;
ordinary edits must not snapshot or diff the complete project. Composite user
gestures appear as one history entry, and previews do not mutate the model until
accepted. See [`adr/003-command-transactions.md`](adr/003-command-transactions.md).

### IDs and source identity are stable

Opaque model IDs are distinct from names and source locations. C++
synchronization preserves IDs through conservative rename/move matching so
diagram presentations, styles, folders, and annotations survive source edits.
Manual/user-owned values take precedence over imported values unless the user
explicitly resolves a conflict in favor of C++.

### Persistence is canonical JSON5 with migrations

A project is a directory containing a manifest, semantic model, and diagram
documents. The Qt-backed JSON5 compatibility layer, serializer, validation,
atomic-save recovery, external-change protection, and migrations are shared by
GUI and headless workflows. Unknown fields are retained for forward
compatibility. Relevant decisions are ADRs 004, 006, and 007.

### Rendering state crosses a deliberate boundary

`DiagramCanvas` is a custom Qt Quick scene-graph item. The GUI/model thread
builds immutable render snapshots; render-thread code must not look up mutable
project state. Keep text atlases, clipping, hit testing, selection overlays,
export, and diagram filtering consistent when adding visual state.

### Scope determines where a setting lives

- Application preferences are machine/user defaults and live in
  `ApplicationSettings`.
- Project-owned definitions such as named styles and stereotype catalogs live
  in the semantic project model.
- Diagram display/filter/layout choices live on the diagram.
- A presentation override affects only that presentation and takes precedence
  over diagram defaults.

Do not add a global preference when the user expects diagrams in one project to
differ.

## Repository map

| Path | Responsibility |
| --- | --- |
| `src/core/project_data.*` | Persisted semantic and presentation records |
| `src/core/project_controller.*` | QML-facing orchestration and command entry points |
| `src/core/project_command*` | Command stack and concrete reversible mutations |
| `src/core/project_serializer*`, `project_schema*`, `json5*` | Storage, validation, recovery, migration |
| `src/core/cpp_import*` | libclang discovery, matching, preview, synchronization |
| `src/core/project_tree_model*` | Hierarchical browser projection and selection data |
| `src/ui/diagram_canvas.*` | Scene-graph rendering, hit testing, direct interaction |
| `src/ui/diagram_arrangement.*` | Automatic rectangle layout |
| `src/ui/connector_routing.*` | Obstacle-aware connector routing and optimization |
| `src/ui/sequence_diagram_layout.*` | Shared deterministic sequence geometry |
| `src/core/activity_diagram.*`, `src/core/project_controller_activity.cpp` | Diagram-local activity records and undoable authoring |
| `src/ui/activity_diagram_layout.*`, `src/ui/diagram_canvas_activity.cpp` | Activity geometry, hit testing, and canvas gestures |
| `src/ui/diagram_image_exporter.*` | PNG and PDF through the production renderer |
| `src/ui/diagram_document_exporter.*` | Vector SVG and editable Draw.io scene export |
| `qml/` | Main shell, diagram windows/views, menus, dialogs, toolboxes |
| `src/app/` | GUI and headless executable entry points |
| `tests/` | Core, canvas, QML, rendered-image, CLI, and smoke regression tests |
| `examples/` | Sample, performance, and sequence projects/interchange |
| `assets/` | Action and project-tree icon catalog plus product assets |
| `installer/`, `tools/` | Packaging, installer, update repository, support scripts |
| `.github/workflows/windows.yml` | Authoritative clean Windows build/test/package/release pipeline |

Large controller or canvas files are not an invitation to add unrelated logic.
Put reusable geometry, routing, serialization, or domain behavior in a cohesive
module and keep QML primarily declarative.

## Build and test on Windows

Use PowerShell 7, Visual Studio 2022, a matching MSVC Qt kit, CMake, and LLVM.
The accepted local configuration is:

```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 `
  -DYAUML_REQUIRE_LIBCLANG=ON
cmake --build build-release --config Release --parallel
ctest --test-dir build-release -C Release --output-on-failure
```

Adjust only the Qt path to the installed kit. Useful focused runs are:

```powershell
ctest --test-dir build-release -C Release -R yauml_core_tests --output-on-failure
ctest --test-dir build-release -C Release -R yauml_canvas_tests --output-on-failure
ctest --test-dir build-release -C Release -R yauml_render_regression --output-on-failure
ctest --test-dir build-release -C Release -R yauml_ui_smoke --output-on-failure
```

Use CTest rather than launching Qt test executables directly. The GUI is
`build-release/Release/yauml.exe`; the console tool is
`build-release/Release/yauml-cli.exe`.

The complete suite currently covers 11 CTest entries: core, canvas, QML
components, rendering, CLI validation, performance example validation,
sequence import/demo validation, C++ preview, UI smoke, and the centralized
color-literal guard. A feature slice should add focused assertions to the most
relevant existing executable before creating another test target.

The full configure/build/install/launch procedure, external machine values,
expected success evidence, common errors, and WSL checks are in
[`platform-runbook.md`](platform-runbook.md).

## Git workflow and multi-PC continuation

- `main` is the integration branch.
- Use short-lived `codex/<topic>` branches for ordinary work.
- A pull request with the Windows CI check is the default delivery path.
- Direct push to `main` occurs only when the owner explicitly requests it.
- Start a slice from an up-to-date integration branch:

  ```powershell
  git fetch --prune origin
  git switch main
  git pull --ff-only origin main
  git switch -c codex/connector-labels
  ```

- To resume an already pushed topic branch on another PC, fast-forward to the
  remote branch before editing:

  ```powershell
  git fetch --prune origin
  git switch codex/connector-labels
  git merge --ff-only origin/codex/connector-labels
  git status --short
  git log --oneline --decorate -8
  ```

- Rebase only private, unshared topic-branch commits. Once a branch/commit has
  been pushed for collaboration, bring in `origin/main` with a normal merge and
  update the pull request rather than rewriting shared history.
- Never force-push `main`, a release tag, or a branch another session may be
  using. Never move or reuse a published version tag.
- Continue work on another PC only from pushed commits. Uncommitted files are
  machine-local; record that fact honestly and either submit them with owner
  authorization or transfer them outside the Git workflow explicitly.
- Before switching machines, update the rolling handover in the same submitted
  commit. On the receiving machine, read `AGENTS.md`, the stable context, and
  the handover before editing.

## Working conventions and common traps

- Preserve all pre-existing dirty and untracked files. In particular, local
  project directories may be user test data even when their names resemble
  fixtures.
- `Main.qml` coordinates the application shell, but commands that depend on a
  diagram must be routed to the focused `DiagramView`, including detached
  windows. Avoid deriving the target from whichever main-window tab happens to
  be selected.
- Relationship endpoint attachment, snap-point identity/provenance, routing,
  and annotation placement are separate state. Geometry changes must preserve
  these invariants through save/load and undo/redo.
- Imported data has explicit provenance and user-authority baselines. Never
  overwrite a manual edit merely because a rescan found a different source
  value.
- Centralize application colors in the theme palette and diagram colors in the
  project style system. `yauml_color_literal_guard` rejects new scattered color
  literals.
- Add icons through the catalog documented in `assets/icons/README.md` and
  verify both local-resource and packaged-resource paths.
- A QML dialog must be checked for keyboard acceptance, modal/window lifetime,
  focus restoration, and binding-loop warnings, not just mouse interaction.
- A rendered-image failure on CI uploads expected, actual, and difference
  artifacts. Inspect them before changing tolerances or baselines.
- Do not edit canonical project JSON5 as a shortcut for sequence authoring.
  Use the validated sequence interchange workflow described below.

## Documentation routes

- Current work and resume point: [`current-handover.md`](current-handover.md)
- Delivered feature inventory and deferred product work:
  [`productization-plan.md`](productization-plan.md)
- Architecture rationale: [`product-architecture-brainstorm.md`](product-architecture-brainstorm.md)
- Command model: [`adr/003-command-transactions.md`](adr/003-command-transactions.md)
- JSON5 boundary: [`adr/004-json5-compatibility-layer.md`](adr/004-json5-compatibility-layer.md)
- Import authority: [`adr/005-cpp-import-authority.md`](adr/005-cpp-import-authority.md)
- Schema migrations: [`adr/006-project-schema-migrations.md`](adr/006-project-schema-migrations.md)
- Save/external-change safety: [`adr/007-external-project-change-safety.md`](adr/007-external-project-change-safety.md)
- Sequence interchange decision: [`adr/008-sequence-diagram-interchange.md`](adr/008-sequence-diagram-interchange.md)
- Sequence interchange usage: [`sequence-diagram-import.md`](sequence-diagram-import.md)
- Activity authoring and compatibility: [`activity-diagrams.md`](activity-diagrams.md)
- Windows release/update pipeline: [`releasing.md`](releasing.md)
- Platform setup and cross-OS verification: [`platform-runbook.md`](platform-runbook.md)

The project-scoped `create-yauml-sequence-diagram` skill must be used when a
request is to analyze source behavior and generate/import a sequence diagram.

## Definition of done for a feature slice

1. Behavior and ownership are placed in the correct architectural layer.
2. All mutations are reversible commands with focused regression coverage.
3. Persisted changes include validation, migration, and round-trip tests.
4. UI changes cover detached/focused diagram routing and keyboard behavior.
5. Relevant documentation is updated without duplicating the full roadmap.
6. The MSVC Release build and appropriate focused tests pass; the complete
   CTest suite passes before submission unless a documented blocker prevents it.
7. On submit, `current-handover.md` is refreshed in the same commit according
   to the root `AGENTS.md` protocol.
