# yauml MVP Acceptance Checklist

Use this checklist for a release-candidate audit against the accepted MVP scope.
Run the cases in order because later cases reuse the scratch project created in
the earlier cases.

This is a go/no-go checklist. If a required action has no usable control, mark
the case **Fail**. Do not mark it Not Applicable merely because the feature is
not implemented.

## Audit record

- Tester: ______________________________
- Date: ________________________________
- Git commit or working-tree description: ________________________________
- Windows version: _____________________
- Display scaling: _____________________
- Number of monitors: __________________
- GPU: _________________________________
- Scratch project directory: _____________________________________________

Status notation:

- `[+] Pass` — every action produced the stated result.
- `[-] Fail` — at least one result differed; record a defect.
- `[x] Blocked` — the test could not be completed for an external reason.

MVP acceptance requires every **Required** case to pass. A crash, data loss,
silent overwrite, broken undo/redo state, or unusable cross-window workflow is
an automatic release blocker.

## 0. Safety and preparation

- [+] Use a new scratch project. Do not perform destructive tests on a real
      project.
- [+] Close other copies of `yauml.exe`.
- [+] Open PowerShell in the repository root.
- [-] Confirm that `build-release/Release/yauml.exe` exists (MSVC Release build).
- [+] If multi-monitor testing is available, connect and enable the additional
      monitor before starting.

## 1. Release build and automated gate — Required

If `build-release` does not exist, configure it first:

```powershell
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64
```

Configuration should end with `Build files have been written to` followed by
the repository's `build-release` directory. Configure again after deleting that
directory or when changing the Qt kit, compiler, or generator. The build
configuration is selected with `--config Release` in the next command because
Visual Studio is a multi-configuration generator.

Then build and test:

```powershell
cmake --build build-release --config Release
ctest --test-dir build-release -C Release --output-on-failure --timeout 30
```

Expected:

- The Release build completes without a compile or link error.
- All CTest entries pass.
- At the current baseline, the summary is `100% tests passed, 0 tests failed out
  of 4`.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 2. Startup and basic window layout — Required

Run:

```powershell
.\build-release\Release\yauml.exe
```

Actions and expected results:

- [+] The application opens without a terminal, DLL, QML, or plugin error.
- [+] The main window contains a project tree on the left, a tabbed diagram area
      in the center, and Selected element properties on the right.
- [+] Drag both panel dividers. Each panel resizes and the center remains usable.
- [+] Use **View > Project tree** twice. The left panel hides and returns.
- [+] Use **View > Properties** twice. The right panel hides and returns.
- [+] Use **View > Log**. The log opens as a pop-up rather than a permanent
      fourth column; close it with Escape.
- [+] Create or select an object. The corresponding project-tree row is visibly
      highlighted.

Result: [ ] Pass  [-] Fail  [ ] Blocked

Notes/defect: The  project tree is iniitialliy collapsed so it does not highlight selected on a diagram element

## 3. Semantic element creation and property editing — Required

Use the toolbar to create one package, two classes, one struct, and one
enumeration.

Actions and expected results:

- [+] Every created object appears in the project tree with a unique name.
- [+] Class, struct, and enumeration presentations appear on the active diagram.
- [+] Rename each object in Selected element properties; the tree and diagram
      update consistently.
- [+] Give a class at least two attributes and two operations using the property
      panel. Each line appears exactly once on the diagram.
- [+] Give the enumeration at least three literals. Each literal appears exactly
      once.
- [+] Add representative visibility, parameter, return-type, and modifier text
      supported by the MVP model. Save the exact values for the reopen check.
- [+] Select each object from both the tree and the diagram. The property panel
      always describes the current object.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect:
 1. after adding any new element or editing existing - the project tree collapses. [x]
 2. Confirming multiline text in property the panel is not obvious - there should appera ok/cancel buttons or so. [x]
 3. atributes in class/struct rectangle is not verticaly centered in its section. The same for operations - it starts way lower than separating line.

## 4. Multiple diagrams and shared semantics — Required

Actions and expected results:

- [+] Click **+ Diagram** twice so the project has at least three diagrams.
- [+] Rename the diagrams `Overview`, `Details`, and `Ports`.
- [+] Select one existing class in the project tree, activate `Details`, then
      double-click the class in the tree. Its presentation is added to `Details`.
- [+] Repeat for `Ports`. The same semantic class now appears on three diagrams.
- [+] Move and resize the three presentations to visibly different geometries.
- [+] Rename the shared class on any diagram. Its name changes on every diagram.
- [+] Switch among the tabs. Each diagram retains its own position and size for
      the shared class.
- [+] Try adding the same class to the same diagram again. No duplicate is
      created and a clear warning is logged.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect:
1. Activating diagram via tab does not affect its highlight in project tree


## 5. Canvas interaction and rendering — Required

Actions and expected results:

- [+] Click an object to select it; Ctrl-click another object to multi-select.
- [+] Drag selected objects. Movement is responsive and commits once when the
      gesture ends.
- [+] Resize an object with its resize handle. The minimum size remains usable.
- [+] Pan the canvas and zoom in/out with the supported pointer gestures.
- [+] Click **Fit**. All diagram content becomes visible.
- [+] At several zoom levels, grid lines remain evenly spaced and do not form
      irregular thick/thin bands.
- [+] At intermediate zoom, text/details remain visible while still legible;
      detail does not disappear unnecessarily early.
- [+] Zoom in substantially. Class and connector-label text remains sharp rather
      than looking like an enlarged bitmap.
- [+] Stack two class rectangles with substantial overlap. Text belonging to the
      lower rectangle is hidden beneath the upper rectangle.
- [+] Verify connector labels have a transparent background and do not erase the
      grid or connector behind them.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 6. Relationships, reconnection, and ports — Required

Create and retain examples of every relationship included in the MVP scope:
dependency, generalization, and association. For each one, select the intended
source node first, Ctrl-click the intended target node, choose the relationship
type beside **Connect**, and click **Connect**.

Actions and expected results:

- [+] Select two different classifier presentations in source-to-target order,
      choose each available relationship type, and click **Connect**.
- [-] The relationship is created once, attaches to rectangle edges, and has the
      correct direction and type.
- [+] Select the connector. Selection is visible and the property panel identifies
      the relationship.
- [+] Rename the relationship. Its diagram label updates.
- [-] Select the connector, click **Source…**, then click a third classifier.
- [-] Select the connector, click **Target…**, then click the new target.
- [+] Drag both endpoint ports to different positions along the classifier
      perimeter. Each endpoint stays on an edge and follows its classifier when
      that classifier moves.
- [+] Move and resize the connected classifiers. Connector endpoints remain
      attached and the line remains visually smooth.
- [+] Record the selected edge and approximate port position for the reopen
      check.

Result: [ ] Pass  [-] Fail  [ ] Blocked

Notes/defect:
1. Direction is always the same regardless of selecting sequence.[x]
2. not clear how to work with "Source/Terget" - what should be selected[x]
3. after creating connection ut is attached toi the center of rectangles - only after manual setting/adjusting conection point/port the line sticks to it.

## 7. In-place editing — Required

Run this for a classifier name, an attribute, an operation, an enumeration
literal, and a relationship label.

For each text value:

- [+] Start editing directly on the diagram without using the property panel.
- [+] The editor uses the same apparent font size and weight as the rendered text.
- [+] Type a change and press Escape. The editor closes and the original model
      value remains unchanged.
- [+] Start again, type a change, and click outside the editor. The change is
      committed.
- [+] The tree/property panel updates where applicable.
- [+] One Undo restores the exact original value; one Redo restores the change.
- [+] No extra undo entry is produced merely by opening or cancelling the editor.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect:
1. in-place editor text appears not at the same line as the original text but a bit lower

## 8. Undo, redo, delete, and command integrity — Required

Test each mutation separately: create, rename, move, multi-move, resize, create
relationship, reconnect source, reconnect target, move a port, add to diagram,
remove a presentation from a diagram, and delete a model object.

For every mutation:

- [+] Capture the visible state before the action.
- [+] Perform exactly one action.
- [+] One Undo restores the exact previous semantic and presentation state.
- [+] One Redo restores the exact resulting state.
- [+] Repeated Undo/Redo does not duplicate objects, connectors, or log entries.

Additional deletion checks:

- [+] Select a classifier presentation and click **Remove** (or press Delete
      while the canvas has focus). Only that presentation and its local
      connector presentations are removed; the classifier and its appearances
      on other diagrams remain.
- [+] Deleting a relationship removes its connector but not either classifier.
- [+] Select a classifier and use **Edit > Delete selected model object**.
      The semantic classifier, all of its presentations, and affected
      relationships are removed without leaving broken connectors.
- [+] Attempting to delete the last diagram is refused with an actionable error.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect:
1. Deleting presentation deletes the classifier - it disapears from all diagrams and project tree. It's not correct
2. The error is not visible in the log window if it's  the last line - it almost completelly behind the bottom edge of the log panel


## 9. Detachable tabs and multiple windows — Required

Use at least three diagrams and two diagram-area windows.

Actions and expected results:

- [+] Drag a tab out of the main tab bar. A standalone diagram-area window opens
      under/near the drop position with that diagram and its tab.
- [+] Move and resize the standalone window.
- [+] Drag a second main-window tab into the standalone diagram area. It becomes
      a tab in that existing window.
- [+] The receiving window keeps exactly the same position and size.
- [+] Drag one tab out of the standalone window to create another standalone
      window. The source window keeps its position and size.
- [+] Drag a tab from one standalone window into the other. The existing target
      window keeps its position and size.
- [+] Drag a tab back into the main diagram area. No semantic or presentation
      object is cloned or lost.
- [+] Select an object in a standalone window. The main-window property panel
      follows that selection.
- [-] Edit diagrams in two windows and confirm both share the same Undo/Redo
      history and project dirty state.
- [+] If two monitors are available, place windows on different monitors and use
      both without unexpected movement, resizing, or scaling changes.
- [+] Close a standalone window containing multiple tabs. Every contained diagram
      returns to the main tab bar and remains editable.

Result: [ ] Pass  [-] Fail  [ ] Blocked

Notes/defect:
1. when tab is dragged outside the app window the mouse cursor becomehas "not allowed" shape. Drop works though
2. it's not obvious diagram on which window is "active" and where a new item will be created by pressing "+..."
3. for non main window diagram undo does not work
4. closing main window should close standalone windows  - now they-re left



## 10. Save, reopen, and data retention — Required

Save the scratch project to the directory recorded in the audit header.

Actions and expected results:

- [+] Saving completes without an error and clears the dirty marker.
- [+] The directory contains `manifest.json5`, `model/model.json5`, and
      `diagrams/diagrams.json5`.
- [+] Close the application and reopen the saved project with:

```powershell
.\build-release\Release\yauml.exe "<scratch-project-directory>"
```

- [+] Project/diagram names, element types, attributes, operations, parameters,
      modifiers, enumeration literals, relationships, and labels are unchanged.
- [+] Every diagram contains the correct presentations with independent geometry.
- [+] Connector endpoint edges and normalized port positions are retained.
- [+] No IDs or objects were regenerated merely by saving and reopening.
- [+] Workspace placement is not required across restart; panel sizes and detached
      windows may return to defaults because workspace restoration is post-MVP.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 11. Deterministic unchanged save — Required

Set the saved project path and capture hashes:

```powershell
$AuditProject = "<scratch-project-directory>"
$before = Get-ChildItem -LiteralPath $AuditProject -Recurse -File |
  Sort-Object FullName |
  ForEach-Object { "{0} {1}" -f $_.FullName, (Get-FileHash -LiteralPath $_.FullName).Hash }
```

Without changing anything, click **Save** again, then run:

```powershell
$after = Get-ChildItem -LiteralPath $AuditProject -Recurse -File |
  Sort-Object FullName |
  ForEach-Object { "{0} {1}" -f $_.FullName, (Get-FileHash -LiteralPath $_.FullName).Hash }
Compare-Object $before $after
```

Expected:

- [+] `Compare-Object` produces no output.
- [+] The GUI log reports that the project already matches the saved files.

Result: [+] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 12. Headless validation — Required

Run:

```powershell
.\build-release\Release\yauml.exe validate .\examples\sample.yauml
.\build-release\Release\yauml.exe validate .\examples\performance.yauml
```

Expected:

- [+] Both commands complete successfully with exit code `0`.
- [+] Diagnostics are suitable for terminal/automation use and do not open the
      GUI.


Create an isolated invalid fixture:

```powershell
$InvalidProject = Join-Path $env:TEMP ("yauml-invalid-" + [guid]::NewGuid() + ".yauml")
Copy-Item -LiteralPath .\examples\sample.yauml -Destination $InvalidProject -Recurse
$DiagramFile = Join-Path $InvalidProject "diagrams\diagrams.json5"
$Broken = (Get-Content -LiteralPath $DiagramFile -Raw) -replace `
  "019b0000-0000-7000-8000-000000000010", "missing-element"
Set-Content -LiteralPath $DiagramFile -Value $Broken -Encoding utf8
.\build-release\Release\yauml.exe validate $InvalidProject
$LASTEXITCODE
```

Expected:

- [+] Validation reports the broken element reference with enough context to
      locate the problem.
- [+] The exit code is nonzero.

Result: [ ] Pass  [ ] Fail  [+] Blocked

Notes/defect:
1. .\build-release\Release\yauml.exe validate .\examples\performance.yauml command produces unreadable characters in output:
 "PS D:\DatWork\uaf\2026\yauml> .\build-release\Release\yauml.exe validate .\examples\performance.yauml
Valid yauml project: Performance Example ΓÇö 600 classes (600 elements, 1 diagrams)"

## 13. GUI diagnostics and automatic log opening — Required

Using the invalid fixture from case 12:

```powershell
.\build-release\Release\yauml.exe $InvalidProject
```

Expected:

- [-] The project is not silently accepted as valid.
- [-] A new error automatically opens the log pop-up.
- [ ] Each entry shows severity, category, and an actionable message.
- [ ] Opening the log does not clear the active diagram selection.
- [ ] Close the log and provoke a command error, such as attempting to delete
      the last diagram. The log opens again.
- [ ] If an in-place editor is active when a new error appears, its text and edit
      state are not lost.

Result: [ ] Pass  [ ] Fail  [x] Blocked

Notes/defect:
It just opened blank project


## 14. JSON5 hand-edit protection and unknown fields — Required

Perform these checks only on disposable copies of the sample project.

- [ ] Add an unknown field such as `"futureAuditField": 42` to a model object,
      open the project, make a normal edit, and save. The unknown field remains.
- [ ] Add a JSON5 comment to a model file, open it, and attempt a save after a
      normal edit. The application refuses any rewrite that would silently remove
      the comment and reports a clear diagnostic.
- [ ] JSON5 comments, trailing commas, single-quoted strings, and unquoted keys
      are accepted when loading.
- [ ] Schema-version and broken-reference errors are reported rather than
      ignored or repaired silently.

Result: [ ] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 15. Interrupted-save recovery — Required

The routine acceptance run uses the controlled automated recovery test from case
1 rather than terminating the application during a real write.

- [ ] `yauml_core_tests` passed its interrupted-save recovery case.
- [ ] Recovery restores the last valid semantic and diagram files as one
      consistent project state.
- [ ] No recovery test overwrote the scratch project's only valid copy.

Result: [ ] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 16. Performance reference diagram — Required

Run:

```powershell
.\build-release\Release\yauml.exe .\examples\performance.yauml
```

The current reference contains 600 nodes and 1,150 connectors on one diagram.

Actions and expected results:

- [ ] Initial load completes without a crash or prolonged UI freeze.
- [ ] Pan continuously across the diagram for at least 15 seconds.
- [ ] Zoom continuously in and out for at least 15 seconds.
- [ ] Select and move representative nodes at low, medium, and high zoom.
- [ ] Interaction remains responsive enough for normal editing; record visible
      stutter rather than judging only the average frame rate.
- [ ] Text becomes more detailed as zoom increases and remains sharp at high zoom.
- [ ] Grid spacing remains visually even throughout the zoom range.
- [ ] No stale text, selection handles, or connector labels remain after objects
      move off screen.

Observed hardware/result: ___________________________________________________

Result: [ ] Pass  [ ] Fail  [ ] Blocked

Notes/defect: ______________________________________________________________

## 17. Final release decision

- [ ] Every Required case passed.
- [ ] No crash, data-loss, or silent-overwrite defect remains open.
- [ ] Every failed or blocked step has a recorded defect and reproduction.
- [ ] Post-MVP items were not counted as MVP failures: source synchronization,
      SVG/PNG/PDF export, automatic layout, orthogonal routing, persisted
      workspace restoration, or non-Windows packaging.

Decision: [+] Accept MVP  [ ] Reject MVP  [ ] Conditional acceptance

Decision notes: Accepted by the product owner on 2026-07-21. Recorded audit
observations that were not release blockers move to the productization backlog;
the MVP scope is closed and post-MVP Phase 3 work may proceed.

## Defect record template

Copy this block for every failed step:

```text
Checklist case and step:
Severity: blocker / major / minor
Build/commit:
Preconditions:
Exact actions:
Expected result:
Actual result:
Reproducibility: always / intermittent / once
Project or fixture:
Screenshot/log attachment:
```
