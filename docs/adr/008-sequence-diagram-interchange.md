# ADR 008: Sequence diagram interchange boundary

Status: accepted

## Context

Static analysis cannot reliably reconstruct runtime ordering, callback
executors, or thread identity for a non-trivial C++ application. Codex can use
the source tree and a user's architectural intent to produce a useful scenario,
but allowing it to edit canonical project JSON5 directly would expose stable
IDs, cross-document references, migrations, and persistence invariants.

## Decision

- Add sequence diagrams as an explicit diagram type in the project domain.
- Keep participants, messages, execution contexts, source evidence,
  activations, and combined fragments diagram-local; they are not structural
  model relationships.
- Provide a small versioned `.sequence.json5` interchange format using readable
  keys and model references by stable ID or fully qualified name.
- Resolve references, allocate IDs, validate, and persist only through the
  shared core service and `yauml-cli sequence-validate` / `sequence-import`.
- Preserve a distinction between confirmed and inferred source evidence.
- Represent threads, event loops, executors, and tasks as execution contexts;
  participants may have defaults and activations may override them.
- Reject duplicate import keys instead of implicitly overwriting a diagram that
  may have been edited by a user.
- Build the project-scoped Codex skill on this public boundary and prohibit it
  from directly editing canonical model or diagram files.

## Consequences

The interchange schema can remain small and friendly while canonical storage
retains stable identity and forward-compatible unknown fields. GUI import can
later wrap the same conversion in one polymorphic undo command. Runtime behavior
produced from source remains an evidence-backed proposal rather than an
unqualified claim of automatic recognition.
