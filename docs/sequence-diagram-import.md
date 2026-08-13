# Sequence diagram import format

Status: version 1 implemented for headless validation/import and the first
interactive Qt Quick rendering slice.

The sequence interchange format is a compact authoring boundary for people,
Codex, and other tools. It deliberately uses readable keys and model references;
yauml resolves those references, assigns stable IDs, validates the complete
project, and writes the canonical project JSON5 files.

Do not write generated sequence records directly into
`diagrams/diagrams.json5`.

## Commands

From the desktop application, open a project and choose **+ Diagram…** on the
main toolbar (or **New diagram…** from the **Diagrams** project-tree menu),
then select **Sequence diagram**. yauml creates its first free participant so
the diagram is valid immediately; add and configure further participants,
messages, activations, and fragments on the canvas.

Choose **File > Import Sequence Diagram…** to bring in an interchange file.
The import is a normal undoable project command. The standalone example can be
imported into any project:

`examples/sequence-import/standalone-demo.sequence.json5`

The same workflow is available headlessly:

```powershell
yauml-cli sequence-validate <project.yauml> <file.sequence.json5>
yauml-cli sequence-import <project.yauml> <file.sequence.json5>
```

Import refuses external project-file changes detected after loading. The
explicit `--overwrite-external-changes` option has the same meaning as for the
C++ headless importer.

## Complete example

```json5
{
  format: "yauml-sequence",
  version: 1,
  diagram: {
    key: "submit-work",
    name: "Submit work",

    contexts: [
      { key: "ui", name: "UI thread", kind: "thread" },
      { key: "pool", name: "Worker pool", kind: "executor" },
    ],

    participants: [
      {
        key: "controller",
        element: "sample::Controller",
        context: "ui",
      },
      {
        key: "worker",
        element: "sample::Worker",
        context: "pool",
      },
    ],

    messages: [
      {
        key: "submit",
        from: "controller",
        to: "worker",
        kind: "async",
        text: "submit()",
        source: {
          file: "src/controller.cpp",
          line: 42,
          confidence: "confirmed",
        },
      },
      {
        key: "complete",
        from: "worker",
        to: "controller",
        kind: "return",
        text: "completed",
        source: {
          confidence: "inferred",
          note: "The callback executor is selected at runtime",
        },
      },
    ],

    activations: [
      {
        key: "worker-run",
        participant: "worker",
        context: "pool",
        start: "submit",
        end: "complete",
      },
    ],

    fragments: [
      {
        key: "success-or-failure",
        kind: "alt",
        participants: ["controller", "worker"],
        start: "submit",
        end: "complete",
        operands: [
          { start: "submit", guard: "accepted" },
          { start: "complete", guard: "rejected" },
        ],
      },
    ],
  },
}
```

## Root and diagram

- `format` must be `yauml-sequence`.
- `version` must be `1`.
- `diagram.name` is required.
- `diagram.key` is optional but recommended. It identifies the import intent;
  importing the same key twice is rejected rather than silently replacing a
  manually edited diagram.

Unknown fields are retained in the canonical sequence data where practical.

## Execution contexts

`contexts` is optional. Each item requires a unique `key`, a display `name`,
and one of these `kind` values:

- `thread`
- `event-loop`
- `executor`
- `task`

Contexts express known execution environments. They do not themselves assert
that two operations run concurrently; use message semantics and, later, UML
combined fragments for that assertion.

## Participants

`participants` is required and must contain at least one item.

- `key`: required unique alias used by messages and activations.
- `element`: optional model element stable ID or exact fully qualified name.
- `name`: optional display name. It is required for a free participant and is
  derived from a resolved model element when omitted.
- `context`: optional default execution-context key.
- `x`, `width`: optional presentation overrides. Defaults provide deterministic
  left-to-right placement.

An unresolved or ambiguous `element` reference is an error with the original
reference in the diagnostic. Multiple participants may reference the same model
element when they represent different runtime instances.

## Messages

`messages` is required. Array order is interaction order.

- `key`: optional unique alias; defaults to `message-N`.
- `from`, `to`: required participant keys. Self-messages are allowed.
- `kind`: required; `sync`, `async`, `return`, `create`, or `destroy`.
- `text`: optional displayed signature or description.
- `y`: optional finite, non-negative position. Values must increase strictly;
  normally omit it and use deterministic default spacing.
- `source`: optional evidence object with `file`, non-negative `line` and
  `column`, `confidence`, and `note`.

`confidence` is `confirmed` by default. Use `inferred` for behavior reconstructed
from conventions, configuration, or incomplete static evidence.

In the application, a message can additionally be linked to one operation of
its target participant's model type. This is a stable model reference and does
not require displaying methods as project-browser nodes. If a message has no
explicit `source` evidence, **Open in VS Code** navigates to its linked target
operation when that operation has source information.

## Activations

`activations` is optional. Each item describes an execution span:

- `key`: optional unique alias; defaults to `activation-N`.
- `participant`: required participant key.
- `context`: optional execution-context key overriding the participant default.
- `start`: required message key.
- `end`: optional message key; omission continues to the interaction end.
- `label`: optional display label.

Activation bounds use stable message IDs after import, so inserting messages
does not silently redirect an existing activation.

## Combined fragments

`fragments` is optional. It supports common interactive UML combined
fragments: `opt`, `loop`, `alt`, `par`, and `critical`.

- `key`: optional unique alias; defaults to `fragment-N`.
- `kind`: required; `opt`, `loop`, `alt`, `par`, or `critical`.
- `participants`: required ordered array of one or more participant keys. It
  defines the horizontal scope of the frame.
- `start`, `end`: required message keys defining an inclusive vertical span.
- `operands`: ordered operand array. Each operand has a required `start`
  message key and an optional `guard`. The first operand must start at the
  fragment's `start`; later operands must begin in strict message order no
  later than `end`. `alt` and `par` require at least two operands and may have
  any larger number; the other operators require exactly one.

For compatibility, version-one interchange files using `guard`,
`alternativeStart`, and `alternativeGuard` are still accepted and normalized
to `operands` during import. New generators should emit `operands`.

Supported fragment kinds are `opt`, `loop`, `alt`, `par`, and `critical`.
Every later `alt`/`par` operand is rendered with its own divider, making all
branches explicit without requiring the importer to infer concurrency from
source.

## Canonical project representation

Imported diagrams are stored with `type: "sequence"` and a nested `sequence`
object containing stable IDs, participant geometry, messages, execution
contexts, source evidence, and activations. Existing schema-4 diagrams migrate
to schema 5 as explicit `type: "class"` diagrams. Schema 6 adds optional
combined-fragment records; schema 7 adds optional target-operation links on
messages; schema 8 generalizes combined fragments to ordered operand lists;
schema 9 lets pointer-authored messages retain their exact source activation;
schema 10 stores target-operation display mode; and schema 11 stores whether
message-kind stereotypes are visible.

## Current editor behavior

The scene-graph canvas renders participant headers and lifelines, execution
context labels, activation bars, and sync, async, return, create, destroy, and
self messages. Confirmed and inferred evidence have distinct presentation.
Fit-to-content, PNG export, and PDF export use the same deterministic sequence
layout.

Participant headers can be moved horizontally and messages vertically without
changing their order. Drag from a lifeline or activation to another lifeline,
activation, or participant header to create a message at that interaction row.
If the chosen row has insufficient space, later messages move down as part of
the same undo operation. Self-messages use the same gesture. While holding the
mouse button during the drag, press `S` for a synchronous call, `A` for an
asynchronous signal, `R` for a return, `C` for create, or `D` for destroy. The
chosen kind is committed immediately on release with empty optional details
and its `«kind»` is shown beside the preview arrow; omitting the key opens the
full editor.

Activation behavior follows the message kind and is not a separate authoring
option. A synchronous call starts a target activation; a synchronous self-call
from an activation starts a nested activation. An asynchronous signal does not
start one. When another external synchronous call arrives during an activation,
it ends the current span and starts a consecutive top-level activation instead
of adding a second initiating message or visually nesting the new span. An
existing later return boundary transfers to the successor. A return dragged
from an activation closes that chosen activation and must go back to the
participant that opened it. Returning from an outer activation implicitly ends
its open
nested activations at the same row, so decorative nested return arrows are not
required. Destroy messages end an open activation on their target. Message
creation and its activation changes form one undo unit. A self-message dragged
from a specific activation leaves that exact bar. Repeated calls from the same
outer activation create sibling bars at the same visual depth, even while an
earlier sibling is still open. Starting the next sibling implicitly ends those
open descendants at the new message row, and a small visual gap keeps adjacent
sibling bars distinct. Deleting a closing return keeps every span it closed and
makes those activations open-ended again; deleting an activation's start
message still removes the invalid span.

A new external synchronous call replaces the target participant's complete
active execution stack: every currently active nested level ends at the new
message row, then a new top-level activation begins. This rule also applies to
older diagrams whose explicit source-activation links are incomplete.

A create message instead places the target participant header at the message
row and starts its lifeline below the header. This expresses object lifetime
without misusing an activation bar to represent it. A participant may have
only one incoming create message.

A participant linked to a model type shows the short type name as its primary
title and the namespace on a smaller second line. An optional instance alias is
shown as `alias : Type`; the full model identity remains available through the
link and is not duplicated into diagram-local data. Participant aliases and
message text support in-place editing.

Select one or more lifelines, then use **New combined fragment…** from the
diagram context menu to add an `opt`, `loop`, `alt`, `par`, or `critical` frame.
Frames are edited through their border context menu and use message boundaries,
not free pixel coordinates. The UI can additionally create free-positioned
activation bars when no suitable boundary messages exist. Type and container
drops from the project tree create participants; class-diagram node,
relationship, and note creation remain disabled on sequence diagrams.

A selected activation shows resize handles at both ends. Dragging a handle
snaps to nearby messages on the same participant and retains stable message-ID
anchoring. Dropping between messages converts a message-bound activation to
explicit geometry; holding Alt deliberately suppresses message and grid
snapping. Each resize is one undo operation.

**Execution contexts…** in the sequence-diagram menu manages diagram-local
threads, event loops, executors, and tasks. A participant can select one as its
default from its context menu, while an activation may inherit that default or
choose an explicit override. Creating, editing, assigning, and deleting
contexts all use focused undoable commands; deleting a context clears its
assignments and Undo restores them.

Messages linked to target operations support source navigation. The combined-
fragment editor can add and remove ordered `alt`/`par` operands and edit each
guard independently. **Target operation** in the diagram menu controls whether
linked operations are hidden, shown by name, or shown as a full signature. The
participant type's FQDN is never repeated in that message annotation. Operation
selectors show only member signatures and filter incrementally as text is
typed, which keeps imported types with many methods practical to navigate.
**Show message stereotypes** independently controls persistent `«sync»`,
`«async»`, `«return»`, `«create»`, and `«destroy»` labels. New sequence diagrams
show them by default; the quick-gesture preview always shows the chosen kind.

Sequence authoring shortcuts use the same commands and undo history as the
context menus:

- `Ctrl+Shift+P`: add a participant at the viewport center.
- `Ctrl+Shift+M`: create a message between the two selected participants.
- `Ctrl+Shift+A`: create an activation on the selected participant.
- `Ctrl+Shift+G`: create a combined fragment for the selected participants.
- `F2`: edit the selected participant, message, activation, or fragment.
- `Delete`: remove the selected sequence items.
