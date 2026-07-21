# ADR 003: Polymorphic project commands

Status: Accepted

Date: 2026-07-21

## Context

Every semantic and presentation edit must be undoable as one user action. The
MVP used `QUndoStack`, but each history entry retained complete before-and-after
copies of `ProjectData`. That made the behavior correct while making memory use
proportional to the whole project for even a one-node move.

The product also needs one mutation path for property editing, canvas gestures,
arrangement, connector editing, import, and later synchronization. Undo and redo
must emit the same notifications as the initial execution and must preserve the
document's clean position.

## Decision

- `ProjectCommand` is the polymorphic domain-facing undo/redo abstraction. Qt's
  `QUndoStack` owns commands and supplies history position and clean-state
  tracking, but QML does not mutate project data directly.
- Every product operation has a concrete command implementing `execute()` and
  `revert()`. Commands retain only their required IDs, previous and next values,
  inserted records, or removed records and positions.
- Controllers validate requests and completely prepare their inverse state
  before pushing commands. Empty operations are discarded before they reach the
  history stack.
- `ProjectCommand::redo()` and `undo()` are final and dispatch through the
  controller's shared apply-and-notify boundary. Concrete commands cannot emit
  inconsistent intermediate UI notifications.
- One command may span semantic and presentation records. Creating a class, for
  example, inserts its model element and initial node presentation as a single
  action. Cascade deletion commands own exactly the dependent records they
  remove.
- Complete `ProjectData` copying and whole-project difference calculation are
  prohibited for ordinary commands. A command that intentionally affects a
  whole aggregate, such as deleting a diagram, may retain that aggregate.
- Future long-running operations prepare a command or composite command away
  from live state, validate it, then push it once. Their first `redo()` must not
  contain a partially failing discovery phase.

## Consequences

- Command preparation and history memory are proportional to affected records,
  apart from reference discovery until domain indexes are introduced.
- A single gesture produces one undo entry even if it affects several model and
  diagram objects.
- Command classes make domain behavior and inverse behavior explicit, at the
  cost of more types than a generic snapshot command.
- Execute/undo/redo command-cycle tests are required whenever new persistent
  model state or a new mutation command is introduced.
