# Activity diagrams

Use an activity diagram to show a workflow, its alternative paths, and work
that can happen in parallel. The notation follows the activity control-node
symbols in [UML 2.5.1](https://www.omg.org/spec/UML/2.5.1/About-UML).

## Author a workflow

1. Choose **New diagram → Activity diagram**. The canvas starts with an initial
   node.
2. Right-click the canvas and choose **Add activity node**. Actions use rounded
   rectangles. Decisions and merges use diamonds. Forks and joins use bars.
   The activity final is a filled dot inside an outer circle.
3. Hold **Shift** and drag from one node to another to create a control flow.
   Release on empty canvas to choose a new destination node. Choosing a type
   adds the node and the control flow in one undo step. Dismissing the menu
   cancels the pending flow.
4. Double-click an action to edit its name or a flow to edit its guard. You can
   also select the item and press **F2**. Enter a guard such as `valid request`
   without brackets. The diagram displays it as `[valid request]`.
5. Drag nodes to reposition them. Hold **Alt** to bypass grid snapping. Select
   an item and press **Delete** to remove it. Removing a node also removes its
   incident flows in the same undo step.

## Arrange a group

Drag from empty canvas to lasso nodes and flows. **Ctrl-click** toggles an item,
**Shift-drag** from empty canvas adds a region, and **Ctrl+A** selects the whole
diagram. Drag a selected node to move the selected group. The arrow keys nudge
selected nodes, with **Shift** for a larger step.

Right-click a selected node and choose **Arrange**, or use the toolbox above
the selection. Alignment uses the last selected node as its reference.
Distribution requires at least three nodes. **Auto arrange** previews a
layered layout for the selection or the whole diagram. Flow endpoints follow
the nodes, and each edit is one undo step.

Activity diagrams use the normal diagram tabs, detached windows, save/load,
zoom, fit, and PNG/PDF export. SVG/Draw.io export, partitions (swimlanes), object
flows, source-operation links, and interchange import are outside this first
editor slice.

## Model and compatibility

Project schema 13 adds `type: "activity"` with an `activity` object in the
canonical diagram document. Nodes carry stable IDs, a type, an optional
display name (required for actions), and center coordinates. Each flow has a
stable ID, source and target node IDs, and an optional guard. The graph belongs
to one diagram and is independent of class elements and relationships. Changes
record only the affected nodes and flows in the command stack.

Headless project validation checks IDs, positions, and local endpoints. A
control flow cannot enter an initial node or leave an activity final. Partial
graphs are valid while being authored. Guards are displayed as text.

Existing class and sequence diagrams migrate without changes to their diagram
records. Projects saved by this build require a schema-13-capable version of
yauml to reopen.
