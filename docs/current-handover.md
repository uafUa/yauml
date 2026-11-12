# Current development handover

Last refreshed: 2026-11-12

This revision prepares yauml **v0.13.0** on the stable Windows x64 channel.
Check the [GitHub release](https://github.com/uafUa/yauml/releases/tag/v0.13.0)
and the tag workflow for actual publication and deployment status. The stable
[development context](development-context.md) and [agent protocol](../AGENTS.md)
explain how to continue on another machine.

## Delivered in this slice

- **Activity diagrams** support manual workflow authoring with UML action and
  control nodes, editable flow guards, Shift-drag with destination creation,
  mixed selection, group arrangement, undo/redo, and PNG/PDF export. See
  [activity diagrams](activity-diagrams.md).
- Class-diagram connectors can create their destination on drop, with a
  relationship-type picker. The edge toolbox supports click-to-place as well
  as drag-to-place. Diagram menus show creation actions first.
- Sequence diagrams render wide labels in bounded atlas tiles, fit combined
  fragments around complete self-message loops, and put the fragment heading
  above the message label. Pointer hit tests use the arrow or the visible
  label. Stored message coordinates and ranges are unchanged.
- The Windows installer and portable ZIP bundle the sequence authoring skill,
  interchange format, and standalone example. Installer verification targets
  its temporary install instead of replacing an existing copy.
- The repo includes its stable context, rolling handover, Windows and WSL
  runbook, PR-first submission policy, and LF canonical EOL policy.

## Compatibility

Project schema 13 adds the diagram-local activity graph. Existing projects
migrate in memory without altering class or sequence diagram records. Saving
writes schema 13; those saves cannot be opened in yauml 0.12.0 or older. There
is no change to the sequence interchange format.

## Validation baseline

- MSVC 2022 x64 Release, Qt 6.11.1, libclang required:
  `cmake --build build-release --config Release --parallel -- /v:quiet` and
  `ctest --test-dir build-release -C Release --output-on-failure --timeout 60`
  passed all 11 tests for the bumped version.
- Local Markdown link targets, Windows/WSL 2 Git status, index EOL counts,
  and both diff checks were verified. Two existing CRLF worktree SVGs match
  their LF index blobs after canonical conversion. No mass normalization.
- The release workflow gates publication on a clean Windows Server 2022 build
  with Qt 6.10.3, CTest, portable package checks, and IFW installer lifecycle
  verification. See [release instructions](releasing.md).

## Resume guidance

No next feature has been selected. For activity-diagram work, choose a real
workflow before extending the baseline to partitions, object flows, source
links, interchange, or SVG/Draw.io export. Sequence import and editing
guidance is in the [interchange specification](sequence-diagram-import.md).
The installer remains unsigned. Do not commit the local `tst/` project.
