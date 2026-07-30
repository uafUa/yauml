# ADR 006: Project schema migrations

Status: accepted

## Context

Project files must remain openable as the persisted model evolves. Tying file
compatibility to the application version would make upgrades difficult to
reason about, while scattering compatibility branches through deserialization
would leave old and new representations active throughout the domain layer.

A project is stored across the manifest, model, and diagrams documents. Future
changes may therefore need to update several documents together. The GUI and
headless tools must apply exactly the same compatibility rules, preserve
unknown extension fields, and provide actionable diagnostics for files they
cannot safely interpret.

## Decision

- Maintain one explicit project-schema version independently from the
  application version. Increment it only together with a migration from the
  preceding version.
- Run schema migrations on the three parsed JSON documents before constructing
  domain objects. The domain deserializer consequently handles only the current
  schema.
- Implement migrations as sequential, one-version steps. Each step receives all
  three documents so a cross-document change can be performed consistently.
- Treat a genuinely absent `schemaVersion` as legacy version 0. This is the
  unversioned POC form of the existing three-document project and migrates to
  version 1 by adding the canonical marker. An explicit null, string, fraction,
  or negative value is malformed rather than legacy.
- Reject projects newer than the supported schema instead of attempting a
  lossy load. Report malformed, unsupported, and missing-migration-path
  conditions through the shared diagnostic model.
- Apply migrations in memory. Opening a project never writes it implicitly;
  the next explicit save writes the canonical current schema.
- Preserve unknown fields through migration and normal serialization so
  extensions and data introduced by other producers are not discarded.
- Use the same serializer and migrator for the GUI, validation, and headless
  import workflows.

## Consequences

Compatibility policy has one testable boundary, and domain code does not need
version-specific branches. Each new schema version requires one focused
migration plus fixtures that exercise upgrade, canonical save, and reload.
Schema 2 moved conventional stereotypes into the project-owned catalog; schema
3 adds the `local`, `private`, and `api` definitions without duplicating
same-named custom entries or restoring unrelated defaults a user deleted.
Schema 4 promotes operation signature strings to structured semantic records
and migrates both live elements and their C++ synchronization baselines with
matching deterministic operation IDs.

Opening an unversioned project produces an informational log entry and marks the
loaded representation as migrated, but does not itself make an external write.
A future-version project fails early with guidance to update yauml, protecting
data the running application cannot understand.
