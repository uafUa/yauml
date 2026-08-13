import QtQuick
import QtQuick.Controls
import QtTest
import "../../qml" as Yauml

TestCase {
    id: testCase
    name: "SearchableOperationComboBox"
    when: windowShown
    width: 480
    height: 320

    Component {
        id: comboComponent
        Yauml.SearchableOperationComboBox {
            width: 360
            choices: [
                { id: "", label: "No linked operation" },
                { id: "alpha", label: "+ Alpha(): void" },
                { id: "gamma", label: "+ Gamma(value: int): bool" }
            ]
        }
    }

    function test_mouse_click_commits_requested_row() {
        const combo = createTemporaryObject(comboComponent, testCase,
                                            { x: 20, y: 20 })
        verify(combo)
        combo.popup.open()
        tryVerify(function() {
            return combo.popup.visible
                    && combo.popup.contentItem.itemAtIndex(2) !== null
        })
        const row = combo.popup.contentItem.itemAtIndex(2)
        mouseClick(row, row.width / 2, row.height / 2)
        compare(combo.selectedValue, "gamma")
        compare(combo.currentValue, "gamma")
    }

    function test_typing_filters_incrementally() {
        const combo = createTemporaryObject(comboComponent, testCase,
                                            { x: 20, y: 20 })
        verify(combo)
        combo.contentItem.forceActiveFocus()
        combo.contentItem.selectAll()
        keyClick(Qt.Key_G)
        keyClick(Qt.Key_A)
        keyClick(Qt.Key_M)
        keyClick(Qt.Key_M)
        keyClick(Qt.Key_A)
        compare(combo.model.length, 1)
        compare(combo.selectedValue, "gamma")
    }
}
