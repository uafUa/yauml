import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as Yauml

TestCase {
    id: testCase
    name: "DiagramNodeMenu"
    when: windowShown
    width: 480
    height: 420

    property QtObject iconRegistry: QtObject {
        readonly property int defaultSize: 16
        function actionIcon(catalogId) { return "" }
    }

    Component {
        id: destinationComponent
        Yauml.DiagramNodeMenu {
            forDestination: true
            popupType: Popup.Item
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            property string selectedType: ""
            onNodeTypeSelected: function(type) { selectedType = type }
        }
    }

    Component {
        id: backgroundComponent
        Yauml.DiagramCanvasMenu { popupType: Popup.Item }
    }

    function shownItems(menu) {
        const result = []
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.visible && item.height > 0)
                result.push(item)
        }
        return result
    }

    function test_destination_choices_and_activation() {
        const menu = createTemporaryObject(destinationComponent, testCase,
                                           { diagramType: "class", x: 20, y: 20 })
        verify(menu)
        menu.open()
        tryCompare(menu, "opened", true)
        const classItems = shownItems(menu)
        compare(classItems.map(item => item.text), ["Class", "Struct", "Enumeration"])
        mouseClick(classItems[1], classItems[1].width / 2, classItems[1].height / 2)
        tryCompare(menu, "opened", false)
        compare(menu.selectedType, "struct")

        menu.diagramType = "activity"
        menu.selectedType = ""
        menu.open()
        tryCompare(menu, "opened", true)
        compare(shownItems(menu).map(item => item.text),
                ["Action", "Activity final", "Decision", "Merge", "Fork", "Join"])
        keyClick(Qt.Key_Escape)
        tryCompare(menu, "opened", false)
        compare(menu.selectedType, "")
    }

    function test_creation_is_the_first_background_menu_entry() {
        const canvas = { diagramType: "class", filterActive: false,
                         selectedSequenceParticipantCount: 0 }
        const menu = createTemporaryObject(backgroundComponent, testCase,
                                           { canvas: canvas, x: 20, y: 20 })
        verify(menu)
        menu.open()
        tryCompare(menu, "opened", true)
        const items = shownItems(menu)
        compare(items[0].text, "Add element")
        const types = items[0].subMenu
        verify(types)
        types.open()
        tryCompare(types, "opened", true)
        compare(shownItems(types).map(item => item.text),
                ["Class", "Struct", "Enumeration", "Package"])
        menu.close()
    }
}
