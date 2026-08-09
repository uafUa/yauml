---
name: create-yauml-sequence-diagram
description: Analyze a code path or behavioral scenario, create a source-evidenced yauml sequence-diagram interchange file, validate it, and optionally import it into a yauml project. Use for requests to document call flows, asynchronous hand-offs, threads, event loops, executors, callbacks, or interactions between existing project types as a yauml sequence diagram.
---

# Create a yauml Sequence Diagram

Create a compact `.sequence.json5` proposal and let yauml resolve model IDs and
canonical persistence. Never edit `model/model.json5` or
`diagrams/diagrams.json5` directly.

## Workflow

1. Read [`docs/sequence-diagram-import.md`](../../../docs/sequence-diagram-import.md)
   completely before writing the import file.
2. Locate the target `.yauml` project and `yauml-cli`. Prefer the installed CLI;
   in this repository use `build-release/Release/yauml-cli.exe` when available.
3. Trace only the requested scenario. Use fully qualified C++ type names for
   `element` references and source file/line evidence for messages when found.
4. Represent known runtime contexts explicitly as `thread`, `event-loop`,
   `executor`, or `task`. Do not infer a concrete thread merely because a call
   is asynchronous.
5. Mark source-supported behavior `confirmed`. Mark plausible but runtime- or
   configuration-dependent behavior `inferred` and explain it in `source.note`.
6. Write the proposal under `<project>/imports/sequences/<slug>.sequence.json5`
   unless the user names another destination.
7. Validate before import:

   ```powershell
   yauml-cli sequence-validate <project.yauml> <file.sequence.json5>
   ```

8. Correct every validation error. Import only when requested:

   ```powershell
   yauml-cli sequence-import <project.yauml> <file.sequence.json5>
   ```

9. Report the created file, imported diagram name, important inferred behavior,
   and validation result.

## Guardrails

- Prefer a small accurate interaction over an exhaustive speculative one.
- Keep participant keys and message keys stable and readable.
- Preserve call order in the `messages` array; add explicit `y` only for an
  intentional spacing override.
- Use separate participants for distinct runtime instances even when they share
  one model type.
- Use activations to attach execution contexts to spans of messages.
- Treat a duplicate diagram `key` as a safety stop. Do not replace an existing
  imported diagram implicitly.
