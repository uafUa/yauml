# Current development handover

Last refreshed: 2026-11-18

This revision prepares **v0.14.0** for the stable Windows x64 channel. Check
the [release](https://github.com/uafUa/yauml/releases/tag/v0.14.0) and tag workflow
for actual publication and update-site deployment status. Read the stable
[development context](development-context.md) and [agent protocol](../AGENTS.md)
before continuing. Do not commit the local `tst/` project.

## Delivered in this slice

- Class-diagram **Ctrl+A** or **Select all by type…** opens a modal selector.
  Choose everything, entities (all or a type), or relationships (all or a
  relationship type). The captured scope is the selected container, the common
  owner of selected presentations, or the diagram root. Selection is one level
  deep unless **Include nested contents** is checked. A relationship requires
  both endpoint presentations in scope. Diagram filters still apply. Enter
  accepts and Escape cancels. Sequence/activity Ctrl+A is unchanged.
- Libclang parses method bodies and records `body` evidence for type usage in
  inline/out-of-line methods, local variables, expressions, static calls, and
  lambdas. The semantic method owner is the dependency source. Free-function
  and function-local class bodies are not attributed to that outer class.
  Signature evidence takes priority, and structural member/inheritance
  relations suppress redundant dependencies. Body/signature changes reuse the
  existing endpoint-pair identity and user-authority policy.
- **Clear bend points** applies to every selected connector as one route-delta
  undo command, without changing routing mode, endpoints, or semantic
  relationships. Selection containing connectors without bends is allowed.

Durable behavior is recorded in
[ADR 005](adr/005-cpp-import-authority.md) and the
[productization plan](productization-plan.md).

## Validation baseline

MSVC 2022 x64 Release, Qt 6.11.1, libclang required:

```powershell
cmake --build build-release --config Release --parallel -- /v:quiet
ctest --test-dir build-release -C Release --output-on-failure --timeout 60
```

All 11 CTest entries passed. Focused tests cover dialog acceptance/cancellation,
container and recursive scopes, typed/filter-aware selection, multi-connector
clear with undo/redo, and body dependencies in both source-folder and
compilation-database modes. Import tests also cover deduplication, repeat sync,
body-to-signature identity, source locations, missing-source reporting, and
persistence. Git diff checks pass. No user project data was edited during
validation. Local Markdown links resolve, and Windows/WSL Git status and diff
checks agree. The release workflow must also pass its clean Windows build,
CTest, portable-package checks, and installer lifecycle tests before publishing.

## Compatibility and resume

The application is 0.14.0 and project schema stays at 13. New body evidence
uses the existing extensible binding metadata; no migration is needed. A fresh
C++ scan/synchronization is required to discover these additional dependencies.
Full body parsing may take longer and expose additional parser diagnostics;
best-effort include/define limitations still apply.

No next slice is selected. Await user feedback. The installer remains unsigned.
