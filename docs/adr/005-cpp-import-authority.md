# ADR 005: C++ import authority and conflict baselines

Status: accepted

## Context

The model is user-edited and authoritative, but C++ declarations may also change
between imports. Comparing only the current source with the current model cannot
distinguish a source update from a manual model edit. Blind synchronization would
therefore overwrite work or create noisy false conflicts.

GUI preview and the headless tools must also use identical discovery and
authority rules. C++ parsing must be compiler-backed rather than maintained as a
partial handwritten language parser.

## Decision

- Discover declarations through libclang. The primary user workflow accepts
  one or more source folders and synthesizes best-effort translation-unit
  arguments from the discovered C++ files and common include roots. Parent and
  descendant selections are normalized to the broader root, while unrelated
  selections remain independently scoped. Automatically use each applicable
  `compile_commands.json` when available to improve fidelity; it is not a user
  prerequisite.
- Store a `sourceBinding` object in each imported element or relationship's
  extensible metadata. It contains the language, Clang-derived identity, source
  location, and the last imported source-owned fields.
- Source-owned fields in this slice are element type, name, attributes,
  operations, the package implied by a C++ namespace, and the stable enclosing
  type ID for a nested record. C++ namespaces are materialized as ordinary
  source-bound UML package elements rather than a parallel semantic kind.
  Diagram placement and other presentation or user metadata remain outside
  synchronization. Manually changed package or nesting ownership remains
  user-authoritative under the same three-way rules below.
- The project-owned stereotype catalog seeds `local`, `private`, and `api`.
  A class or struct definition originating in a C++ implementation file gains
  the project's `local` stereotype. The binding records that particular
  assignment as import-managed, allowing synchronization to remove it when the
  declaration moves to a header. All other stereotype assignments—including
  manual `private`, `api`, and custom entries—remain user-owned. If the project
  deletes the conventional `local` definition, import disables the rule rather
  than recreating project data.
- Plan each import using a three-way comparison:

  | Current model | Current source | Result |
  | --- | --- | --- |
  | Equals baseline | Equals baseline | Unchanged |
  | Equals baseline | Changed | Safe source update |
  | Changed | Equals baseline | Keep user edit |
  | Changed | Changed differently | Conflict; keep user edit |
  | Same final value | Changed baseline | Refresh binding baseline |

- Never bind an existing unbound same-name user element automatically. Report a
  conflict so a later explicit resolution flow can make that decision visible.
- Do not delete a bound model element merely because its declaration was not
  discovered. Report `missing-source` and retain the model.
- Re-plan immediately before GUI Apply. Discovery may be asynchronous, but a
  stale preview is never trusted to overwrite model state.
- Apply all non-conflicting changes as one polymorphic undo command. Headless
  import uses the same plan but applies it directly before deterministic save.

Direct C++ base specifications are imported as semantic generalization
or realization relationships. Because C++ has no distinct `implements` syntax,
the persisted Interface pattern preference is matched against the unqualified
base name. The default `^I[A-Z].*$` classifies names such as `IService` as
realizations; non-matches are generalizations. Invalid or empty expressions are
rejected. Their stable identity remains the pair of Clang identities for the
derived and base declarations, so changing the preference updates the existing
relationship through the normal authority and undo rules rather than creating a
duplicate. An existing unbound user relationship is never silently claimed as
source-owned. Diagram connectors remain presentation data and are created by the
normal placement workflow when both relationship endpoints appear together.

A record declared inside another record also produces a source-bound
containment relationship from the enclosing type to the nested type. Its stable
identity uses both Clang symbol identities and it follows the same baseline,
conflict, and missing-source rules as all other imported relationships.

Record member and operation-signature types are also imported as semantic
relationships. A by-value member or a member using a configured owning pointer
template is composition. A member using a configured shared pointer template,
raw pointer, or reference is aggregation. An unclassified template wrapper is
association, avoiding an unsupported ownership claim. A type appearing only in
an operation parameter or return type is dependency. Structural member evidence
suppresses a weaker signature dependency for the same source/target pair.
Owning and shared pointer-template names are persisted application preferences;
the defaults are `std::unique_ptr` and `std::shared_ptr`. Type-use relationship
identity uses the source and target Clang identities independently of whether
the current evidence is a member or operation signature, so preference-driven
or source-driven reclassification updates the existing relationship through the
same baseline and conflict rules.

Until rename/move matching and explicit resolution are available, a bound
declaration or relationship that disappears from discovery is reported but not
deleted. This prevents incomplete best-effort parsing or a partial compilation
database from causing destructive model changes.

## Consequences

The initial importer is conservative and repeatable. Manual model edits survive
source evolution, except for the explicitly source-derived `local`
classification, and every conflict is visible in the existing log panel. The
stored baseline also provides the foundation for later explicit resolution,
rename matching, and continuous synchronization.

The source-folder mode trades compiler-exact preprocessing for an immediately
usable import: missing third-party headers and unavailable build-specific
defines may reduce the discovered model, but diagnostics clearly identify the
best-effort mode and no source or user-authored model data is overwritten.
Projects persist the normalized folder list, and legacy singular source-root
settings are read compatibly. Each selected subtree automatically gains
compiler-exact flags if a compilation database is added later.

Clang symbol identities can change on source renames or moves. Those cases are
intentionally deferred to a later rename-matching slice rather than guessed in
this foundation.
