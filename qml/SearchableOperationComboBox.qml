import QtQuick
import QtQuick.Controls

// An editable ComboBox whose edit text is an implicit incremental filter.
// Operation dialogs can therefore keep the compact familiar selector while
// remaining practical for classes with hundreds of methods.
ComboBox {
    id: root

    property var choices: []
    property bool internalUpdate: false
    // Keep the accepted semantic value separate from editText. An editable
    // ComboBox otherwise treats a partially typed query as a custom value,
    // while this selector must always resolve to one operation from `choices`.
    property string selectedValue: ""

    editable: true
    selectTextByMouse: true
    textRole: "label"
    valueRole: "id"
    model: choices

    function indexOfValueIn(items, value) {
        for (let index = 0; index < items.length; ++index) {
            if (items[index].id === value)
                return index
        }
        return -1
    }

    function selectValue(value) {
        internalUpdate = true
        model = choices
        const requestedIndex = indexOfValueIn(choices, value)
        currentIndex = requestedIndex >= 0 ? requestedIndex
                                           : (choices.length > 0 ? 0 : -1)
        selectedValue = currentIndex >= 0 ? choices[currentIndex].id : ""
        editText = currentIndex >= 0 ? currentText : ""
        internalUpdate = false
    }

    function applyFilter(query) {
        const needle = query.trim().toLocaleLowerCase()
        const filtered = needle.length === 0 ? choices : choices.filter(
            function(choice) {
                return (choice.label || "").toLocaleLowerCase().indexOf(
                            needle) >= 0
            })
        internalUpdate = true
        model = filtered
        currentIndex = filtered.length > 0 ? 0 : -1
        selectedValue = filtered.length > 0 ? filtered[0].id : ""
        // Keep the typed query visible rather than replacing it with the first
        // match. The first row remains the value accepted on Enter/OK.
        editText = query
        internalUpdate = false
        if (!popup.visible)
            popup.open()
    }

    onActivated: function(index) {
        // currentValue already represents the row chosen by mouse or keyboard.
        // Restore the complete model only after remembering that value.
        selectValue(index >= 0 ? selectedValue : "")
    }
    onCurrentValueChanged: {
        if (!internalUpdate)
            selectedValue = currentValue || ""
    }
    onChoicesChanged: selectValue(selectedValue)
    onPressedChanged: {
        if (pressed && contentItem && contentItem.selectAll)
            contentItem.selectAll()
    }

    // editText also changes when ComboBox commits a clicked row. Listening to
    // that property rebuilt the model while the popup was dispatching its
    // click, which discarded the chosen operation. textEdited is emitted only
    // for actual typing, so filtering and row selection remain independent.
    Connections {
        target: root.contentItem

        function onTextEdited() {
            if (!root.internalUpdate)
                root.applyFilter(root.contentItem.text)
        }
    }

    Component.onCompleted: selectValue("")
}
