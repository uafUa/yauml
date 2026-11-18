import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as Yauml

TestCase {
    id: testCase
    name: "DiagramSelectionDialog"
    when: windowShown
    width: 520
    height: 520

    property QtObject iconRegistry: QtObject {
        readonly property int defaultSize: 16
        function actionIcon(id) { return "" }
    }

    Component {
        id: dialogComponent
        Yauml.DiagramSelectionDialog { popupType: Popup.Item }
    }
    SignalSpy { id: selectionSpy; signalName: "selectionRequested" }

    function test_categories_scope_and_cancel() {
        const dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog)
        selectionSpy.target = dialog
        selectionSpy.clear()
        dialog.openFor("frame-id", "Selected namespace")
        tryCompare(dialog, "opened", true)
        const category = findChild(dialog, "selectionCategory")
        const type = findChild(dialog, "selectionType")
        const nested = findChild(dialog, "selectionIncludeNested")
        compare(category.currentValue, "all")
        compare(type.visible, false)
        compare(nested.checked, false)
        category.currentIndex = 2
        tryCompare(type, "count", 8)
        type.currentIndex = type.indexOfValue("dependency")
        nested.checked = true
        mouseClick(dialog.standardButton(Dialog.Ok))
        tryCompare(dialog, "opened", false)
        compare(selectionSpy.count, 1)
        compare(Array.from(selectionSpy.signalArguments[0]), ["relations", "dependency", true, "frame-id"])

        dialog.openFor("", "Diagram")
        tryCompare(dialog, "opened", true)
        category.currentIndex = 1
        tryCompare(type, "count", 7)
        type.currentIndex = type.indexOfValue("struct")
        keyClick(Qt.Key_Escape)
        tryCompare(dialog, "opened", false)
        compare(selectionSpy.count, 1)

        dialog.openFor("", "Diagram")
        tryCompare(dialog, "opened", true)
        category.currentIndex = 0
        keyClick(Qt.Key_Return)
        tryCompare(dialog, "opened", false)
        compare(selectionSpy.count, 2)
        compare(Array.from(selectionSpy.signalArguments[1]), ["all", "all", true, ""])
    }
}
