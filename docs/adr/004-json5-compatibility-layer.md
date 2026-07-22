# ADR 004: Qt-backed JSON5 compatibility layer

Status: Accepted

Date: 2026-07-22

## Context

Project files are user-editable JSON5. The application currently stores its
persistence tree in Qt's `QJsonDocument`, preserves unknown fields, and refuses
to overwrite a source file when comments would be lost. The accepted project
profile requires comments, trailing commas, single-quoted strings, and unquoted
identifier keys; it does not require every JSON5 numeric and string extension.

A dependency should reduce parser risk without introducing a second long-lived
JSON value model, weakening integer handling, or making comment-loss detection
less reliable.

The following native open-source options were assessed:

- [`mqnc/json5cpp`](https://github.com/mqnc/json5cpp) implements the full JSON5
  grammar and is small and MIT-licensed, but it has no release artifacts or
  consumable CMake library target. Its value model stores all numbers as
  `double`, so integers above 2^53 cannot round-trip exactly.
- [`plexinc/json5`](https://github.com/plexinc/json5) is a small MIT-licensed
  header-only parser with integer types, but it has no published releases,
  modern CMake package, or meaningful downstream adoption.
- [`MistEO/meojson`](https://github.com/MistEO/meojson) is a better maintained,
  MIT-licensed, zero-dependency header library. Its current public documentation
  clearly supports strict JSON and JSON with comments and trailing commas, but
  does not define a complete JSON5 compatibility contract.
- [`taocpp/json`](https://github.com/taocpp/json) is mature and can parse JAXN,
  a related relaxed-JSON dialect. JAXN is not JSON5, and adopting taoJSON would
  add a broad second JSON stack for a narrow compatibility requirement.

None of these libraries documents a source-preserving edit API that retains
comments as syntax-tree nodes. The application would therefore still need its
own source inspection and safe-save policy even after adopting one of them.

## Decision

- Keep `QJsonDocument` as the sole persistence value model.
- Keep the small lexical compatibility layer in `Json5`: it normalizes only the
  documented project profile before delegating structural parsing, Unicode
  decoding, and number handling to Qt.
- Serialize through Qt first, then omit quotes only from ASCII
  identifier-safe object keys. Keys that require escaping or quoting remain
  quoted, including unknown forward-compatible fields.
- Continue refusing to rewrite files containing comments until source-preserving
  edits are implemented.
- Do not claim full JSON5 grammar support. Features outside the documented
  profile, such as hexadecimal and non-finite numbers, are rejected.
- Reassess the dependency decision if full JSON5 compatibility, syntax-tree
  preservation, or substantially more hand-written parsing becomes a product
  requirement. Any future library remains behind the `Json5` adapter so domain
  and serializer code do not expose third-party types.

## Consequences

- The project avoids a second JSON DOM and retains Qt-native values and errors.
- The supported hand-editing syntax remains intentionally smaller than the full
  JSON5 specification, but it is explicit and regression-tested.
- The lexical layer remains application-owned code and therefore requires tests
  for every accepted syntax feature and serialization policy.
- A future library replacement is localized to the adapter rather than spread
  through project persistence code.
