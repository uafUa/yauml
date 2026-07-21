if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

file(GLOB_RECURSE UI_SOURCE_FILES LIST_DIRECTORIES false
    "${SOURCE_ROOT}/src/*.cpp"
    "${SOURCE_ROOT}/src/*.h"
    "${SOURCE_ROOT}/qml/*.qml"
)

set(VIOLATIONS "")
foreach(SOURCE_FILE IN LISTS UI_SOURCE_FILES)
    file(TO_CMAKE_PATH "${SOURCE_FILE}" NORMALIZED_SOURCE_FILE)
    if(NORMALIZED_SOURCE_FILE MATCHES "/src/ui/ui_theme\\.cpp$")
        continue()
    endif()

    # Transparent is deliberately allowed: it means no paint rather than a
    # product color. All visible color values belong in ui_theme.cpp.
    file(STRINGS "${SOURCE_FILE}" COLOR_LINES
        REGEX "\"#[0-9A-Fa-f]+\"|Qt::(white|black|red|green|blue|gray|yellow|cyan|magenta)|\"(white|black|red|green|blue|gray|grey|yellow|orange|purple|pink|cyan|magenta)\"|QColor[ \\t]*\\([ \\t]*[0-9]|Qt\\.(rgba|hsla)[ \\t]*\\("
    )
    foreach(COLOR_LINE IN LISTS COLOR_LINES)
        file(RELATIVE_PATH RELATIVE_SOURCE "${SOURCE_ROOT}" "${SOURCE_FILE}")
        string(STRIP "${COLOR_LINE}" COLOR_LINE)
        string(APPEND VIOLATIONS "\n  ${RELATIVE_SOURCE}: ${COLOR_LINE}")
    endforeach()
endforeach()

if(VIOLATIONS)
    message(FATAL_ERROR
        "Visible UI color literals must be defined in src/ui/ui_theme.cpp:"
        "${VIOLATIONS}"
    )
endif()
