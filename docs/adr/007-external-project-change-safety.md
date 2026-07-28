# ADR 007: External project change safety

## Status

Accepted and implemented.

## Context

A yauml project is a directory of canonical JSON5 files. Git operations, text
editors, scripts, or another application instance can modify those files after
the project has been opened. Interrupted-save recovery protects against a
partially completed yauml write, but it cannot by itself prevent a later save
from silently replacing valid external work.

The domain model and undo stack must remain independent from this concern.
File timestamps alone are also insufficient because they can have coarse
resolution or be preserved by tooling.

## Decision

Loading records SHA-256 content revisions for every file that produced the
project and for the canonical save targets. These revisions are session state
owned by the persistence/controller boundary; they are not serialized into
`ProjectData` and never enter command history.

Before a save that would change bytes, the serializer compares current files
with the expected revision. A mismatch stops before any project file is
replaced and reports the changed relative paths. The check is repeated after
preparing recovery data to narrow the comparison/write race window.

The GUI offers four explicit outcomes:

- **Save As…** preserves both versions in different directories.
- **Reload** discards in-memory edits and loads the external version.
- **Cancel** changes nothing.
- **Overwrite** intentionally replaces the external version.

Headless C++ import fails with a distinct exit code for the same condition and
supports an explicit overwrite option for controlled unattended workflows.
Saving to a different directory does not apply the old directory's revision.

## Consequences

Normal save remains automatic when files match the loaded revision. External
work is never silently overwritten by a cooperative save path, and the
existing recovery transaction still protects an explicitly accepted write.

This is conflict detection and decision routing, not a JSON merge engine.
Automatic structural merging would require property-level provenance across
all persisted presentation and workspace data and remains outside this slice.

## Manual verification

Use a disposable project:

1. Open it, make an unsaved diagram change, then append harmless whitespace to
   `diagrams/diagrams.json5` in a text editor.
2. Save and verify that the dialog names that relative file and the external
   bytes remain untouched.
3. Verify **Cancel** retains the dirty in-memory project and **Save As…** writes
   a second project directory.
4. Repeat the conflict and verify **Reload** shows the external version.
5. Repeat once more and verify **Overwrite** saves the in-memory version and
   clears the dirty marker.
