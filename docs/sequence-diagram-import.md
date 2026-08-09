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
        guard: "accepted",
        alternativeStart: "complete",
        alternativeGuard: "rejected",
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
- `guard`: optional guard label for the first (or only) operand.
- `alternativeStart`: required for `alt` and `par`; message key where the
  second operand begins. It must fall after `start` and no later than `end`.
- `alternativeGuard`: optional guard label for the second `alt` or `par`
  operand.

Supported fragment kinds are `opt`, `loop`, `alt`, `par`, and `critical`.
`par` is rendered with a second operand divider, making parallel branches
explicit without requiring the importer to infer concurrency from source.

## Canonical project representation

Imported diagrams are stored with `type: "sequence"` and a nested `sequence`
object containing stable IDs, participant geometry, messages, execution
contexts, source evidence, and activations. Existing schema-4 diagrams migrate
to schema 5 as explicit `type: "class"` diagrams. Schema 6 adds optional
combined-fragment records; schema 7 adds optional target-operation links on
messages.

## Current editor behavior

The scene-graph canvas renders participant headers and lifelines, execution
context labels, activation bars, and sync, async, return, create, destroy, and
self messages. Confirmed and inferred evidence have distinct presentation.
Fit-to-content and PNG export use the same deterministic sequence layout.

Participant headers can be moved horizontally and messages vertically without
changing their order. Drag from a lifeline or activation to another lifeline,
activation, or participant header to create a message at that interaction row.
If the chosen row has insufficient space, later messages move down as part of
the same undo operation. Self-messages use the same gesture.

The message editor updates activations automatically by default: synchronous
and asynchronous messages start a target activation; return messages
end the latest open message-bound activation on their source; destroy messages
end one on their target. The checkbox can disable this behavior for a message.
Message creation and its activation change form one undo unit.

A create message instead places the target participant header at the message
row and starts its lifeline below the header. This expresses object lifetime
without misusing an activation bar to represent it.

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

Messages linked to target operations support source navigation. Richer
multi-operand fragment editing remains a later sequence-diagram slice.
