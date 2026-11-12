# yauml repository instructions for coding agents

These instructions are the durable entry point for every Codex session working
in this repository. They exist so work can move between machines and sessions
without relying on conversation history.

## Mandatory orientation

Before changing files:

1. Inspect `git status --short`, the current branch, and the recent commit log.
   Existing modifications and untracked files belong to the user unless the
   current task clearly says otherwise.
2. Read [`docs/development-context.md`](docs/development-context.md) for the
   stable product, architecture, repository, build, and test context.
3. Read [`docs/current-handover.md`](docs/current-handover.md) for the most
   recent delivered slice, current risks, deferred work, and suggested resume
   point.
4. Read only the specifications, ADRs, or feature documents relevant to the
   requested task. The handover contains a documentation routing table.

The implementation and tests are authoritative if a document has become stale.
Correct the stale document as part of the same slice instead of silently
working around it.

## Engineering rules

- Treat all generated and modified code as human-maintained production code.
  Keep responsibilities cohesive, names explicit, and comments focused on
  invariants and non-obvious tradeoffs.
- Preserve the architectural invariants in `docs/development-context.md`, most
  importantly semantic/presentation separation and command-based mutation.
- Add focused execute/undo/redo, persistence, validation, or rendering tests in
  proportion to the changed behavior.
- Do not edit or commit unrelated changes, local projects, build output, or
  user test data. Stage intended paths explicitly.
- Use the MSVC Release build and CTest commands documented in
  `docs/development-context.md`. A directly launched Qt test executable may
  lack the runtime environment that CTest supplies.
- When a product or architecture decision becomes durable, update the relevant
  product document or add an ADR. Do not leave durable decisions only in the
  rolling handover.

## Submit-work protocol

Submission is never implied by an implementation request. Until the owner uses
one of the explicit phrases below, leave changes uncommitted and say so in the
handover and final report.

- **submit work**: finish validation, update the handover, commit the intended
  slice, and push the current topic branch. It does not create a release.
- **commit and push**: identical to **submit work**.
- **publish**: in this repository, publish a GitHub Release from a version tag
  after the normal commit/push and CI gates. If the requested version is
  ambiguous, obtain it from the owner rather than guessing.
- **release**: identical to **publish** unless the owner explicitly asks only
  for release preparation without creating the tag.

Every submission form includes handover maintenance. Perform the following
before the final commit or release tag:

1. Review the complete intended diff and the commits made during the slice.
2. Update `docs/current-handover.md` in the same commit:
   - describe the delivered behavior and important design decisions;
   - record the exact validation performed and its result;
   - list known issues, intentionally deferred work, and any migration or
     compatibility concern;
   - state the most useful next step, or explicitly say that no next slice has
     been selected;
   - remove obsolete transient notes instead of endlessly appending history.
3. Move stable knowledge into `docs/development-context.md`, the productization
   plan, a focused feature document, or an ADR. Keep the handover concise.
4. Re-run the appropriate verification after documentation and code changes.
5. Stage only the intended files, including the handover, then commit and push.
6. Report the pushed commit and verification. For a release, follow
   [`docs/releasing.md`](docs/releasing.md), wait for the tag workflow, and
   verify the published assets before reporting success.

If the user asks to submit work produced by an earlier session, reconstruct the
handover from `git diff`, recent commits, tests, and relevant code. Do not omit
the handover because the original conversation is unavailable.

`docs/current-handover.md` describes the commit that contains it; it therefore
does not need to embed its own commit hash. Use Git for exact revision identity.

Do not describe uncommitted files as portable or available to another machine.
After the first handover-system submission, verify a fresh clone from the remote
contains `AGENTS.md`, `docs/development-context.md`, and
`docs/current-handover.md` before claiming cross-PC availability.

## Documentation ownership

- `README.md`: short product-facing overview and entry points.
- `docs/development-context.md`: stable engineering orientation for a new
  machine or session.
- `docs/current-handover.md`: rolling state at the latest submitted commit.
- `docs/productization-plan.md`: detailed delivered capability ledger and
  longer-term product work.
- `docs/product-architecture-brainstorm.md`: architectural rationale and the
  original broader design direction.
- `docs/adr/`: decisions that should not be rediscovered in later sessions.
- `docs/releasing.md`: authoritative packaging, tag, installer, and update-site
  procedure.
- `docs/platform-runbook.md`: reproducible Windows development, packaging,
  installation, launch, and WSL portability checks.
- `docs/sequence-diagram-import.md` and
  `.agents/skills/create-yauml-sequence-diagram/`: sequence interchange and the
  source-evidenced Codex workflow.
