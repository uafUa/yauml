# Current development handover

Last refreshed: 2026-11-18

This revision prepares **v0.14.1** for the stable Windows x64 channel. Check
the [release](https://github.com/uafUa/yauml/releases/tag/v0.14.1) and tag workflow
for actual publication and update-site deployment status. Read the stable
[development context](development-context.md) and [agent protocol](../AGENTS.md).
Do not commit the local `tst/` project.

## Delivered fix: relationships to class templates

A use of `A<...>` now creates a relationship to the imported primary class
template `A`, as well as retaining the existing relationships to type arguments.
Clang identities resolve instantiated, aliased, dependent, and partially
specialized record uses to the declaration represented by the model.

- By-value fields produce composition to the template object; raw pointers and
  references produce aggregation; method-body and signature uses produce
  dependency. Template bases use the same primary declaration identity.
- A numeric template argument is not the multiplicity of the template object.
  Actual array bounds remain multiplicities. Defaults and empty packs work.
- Pointer/container rules still classify contained arguments separately; they
  do not reclassify the wrapper object itself. External templates outside the
  import scope are not materialized as extra model elements.
- Repeated sync, changing a numeric argument, save/load, import undo/redo, and
  upgrading an older argument-only import preserve relationship identity.

Durable rules are in [ADR 005](adr/005-cpp-import-authority.md) and the
[productization plan](productization-plan.md).

## Validation

MSVC 2022 x64 Release, Qt 6.11.1, libclang required:

```powershell
cmake --build build-release --config Release --parallel -- /v:quiet
$env:TEMP = (Resolve-Path build-release).Path
$env:TMP = $env:TEMP
$env:QT_FONT_DPI = '96'
ctest --test-dir build-release -C Release --output-on-failure --timeout 60
```

All 11 entries passed with the explicit font DPI above. Core tests also passed
without that override, including both source-folder and compilation-database
rows of the new template regression. It covers numeric/type/mixed arguments,
aliases, default arguments, empty packs, dependent uses, partial specialization,
pointer/reference/array fields, method bodies/signatures, inheritance,
classification policy, repeat sync, identity, undo/redo, and persistence.

The default local GUI test environment failed `wideSequenceLabelAtFractionalZoom`
and UI smoke. Both pass with `QT_FONT_DPI=96`. No renderer, QML, test thresholds,
or reference images were changed in this slice. Treat local font-DPI test
sensitivity separately from the importer fix.

The ignored `out/template-number-probe/` reproducer now reports the previously
missing composition, aggregation, and body dependency edges. Preview was
read-only; user projects were not changed. Local Markdown links resolve, and
Windows/WSL Git status and diff checks agree. No mass EOL normalization.

## Resume

No next feature is selected; await user feedback. Re-run C++ synchronization
with the updated executable to discover the new edges. The application is
0.14.1 and project schema remains 13; no migration is needed. The installer
remains unsigned. The release workflow must pass its clean Windows build,
CTest, package checks, and installer lifecycle tests before publication.
