# Action and project-tree icon catalog

Edit [`action-icons.json5`](action-icons.json5) and fill each `svg` value with
a path relative to this directory:

```json5
saveProject: {
  label: "Save project",
  contexts: ["File menu", "main toolbar"],
  svg: "project/save.svg",
},
```

The stable identifier is the enclosing path, such as `project.saveProject`,
`arrange.alignLeft`, or `projectTreeNodes.class`. Labels and UI locations may
change without changing that identifier.

Guidelines:

- Paths use forward slashes and are case-sensitive once packaged.
- Reusing the same SVG path for related actions is allowed.
- Leave `svg` empty when an action should intentionally remain text-only.
- `expandedSvg` is optional. When empty, an expanded tree node uses `svg`.
- Nested-type icons fall back to their normal type icon when left empty.
- Prefer a consistent SVG `viewBox` and transparent background. The intended
  default displayed size is 20×20 logical pixels.
- Avoid embedded raster images, external files, scripts, and fonts.
- Keep important strokes and shapes legible at 16–24 pixels.

The application embeds the catalog and all SVG files below this directory,
validates references at startup, and resolves icons through the stable catalog
IDs. Rebuild after changing the catalog or adding an SVG because Release assets
are compiled into the executable. Empty assignments remain text-only.

Project-tree entries are resolved generically from their `match` rules. The
initial action trial connects `createRelationship.aggregation` and
`createRelationship.composition`; other actions can be connected incrementally
through the same registry as their SVGs are supplied. Dynamic project-style
choices use the single `style.assignNamed` entry; a color swatch can be rendered
beside it without requiring an SVG per user-created style.
