cmake_minimum_required(VERSION 3.24)

project(uuml VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.8)

add_library(uuml_core STATIC
    src/core/diagnostic_model.cpp
    src/core/diagnostic_model.h
    src/core/json5.cpp
    src/core/json5.h
    src/core/project_command.cpp
    src/core/project_command.h
    src/core/project_commands.cpp
    src/core/project_commands.h
    src/core/project_controller.cpp
    src/core/project_controller.h
    src/core/project_data.cpp
    src/core/project_data.h
    src/core/project_serializer.cpp
    src/core/project_serializer.h
    src/core/project_tree_model.cpp
    src/core/project_tree_model.h
    src/core/workspace_controller.cpp
    src/core/workspace_controller.h
)

target_include_directories(uuml_core PUBLIC src)
target_link_libraries(uuml_core PUBLIC Qt6::Core Qt6::Gui)

add_library(uuml_ui_support STATIC
    src/ui/text_occlusion.cpp
    src/ui/text_occlusion.h
    src/ui/triangle_batch.cpp
    src/ui/triangle_batch.h
)
set_target_properties(uuml_ui_support PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)
target_include_directories(uuml_ui_support PUBLIC src)
target_link_libraries(uuml_ui_support PUBLIC Qt6::Core)

add_library(uuml_canvas STATIC
    src/ui/diagram_canvas.cpp
    src/ui/diagram_canvas.h
)
target_include_directories(uuml_canvas PUBLIC src)
target_link_libraries(uuml_canvas PUBLIC
    uuml_core
    uuml_ui_support
    Qt6::Gui
    Qt6::Quick
)

if(MSVC)
    target_compile_options(uuml_canvas PRIVATE /W4 /permissive-)
else()
    target_compile_options(uuml_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(uuml_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(uuml_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(uuml_generate_performance_example
    tools/generate_performance_example.cpp
)
target_link_libraries(uuml_generate_performance_example PRIVATE uuml_core Qt6::Core)

add_custom_target(generate_performance_example
    COMMAND uuml_generate_performance_example
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/performance.uuml" 600
    DEPENDS uuml_generate_performance_example
    COMMENT "Generating the 600-node performance example"
    VERBATIM)

qt_add_executable(uuml
    src/app/main.cpp
)

qt_add_qml_module(uuml
    URI Uuml
    VERSION 1.0
    QML_FILES
        qml/DiagramArea.qml
        qml/DiagramView.qml
        qml/DiagramWindow.qml
        qml/Main.qml
)

target_link_libraries(uuml PRIVATE
    uuml_canvas
    uuml_core
    uuml_ui_support
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)

if(MSVC)
    target_compile_options(uuml PRIVATE /W4 /permissive-)
else()
    target_compile_options(uuml PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    option(UUML_DEPLOY_QT_RUNTIME
        "Copy the Qt runtime beside uuml.exe after linking" ON)
    if(UUML_DEPLOY_QT_RUNTIME)
        get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(QT_RUNTIME_DIRECTORY
            "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT_RUNTIME_DIRECTORY}"
            REQUIRED)
        add_custom_command(TARGET uuml POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations
                --compiler-runtime
                "$<TARGET_FILE:uuml>"
            COMMENT "Deploying the Qt runtime beside uuml.exe"
            VERBATIM)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(uuml_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(uuml_core_tests PRIVATE
        uuml_core
        uuml_ui_support
        Qt6::Test
    )
    if(WIN32)
        add_custom_command(TARGET uuml_core_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:uuml_core_tests>"
            COMMENT "Deploying the Qt test runtime"
            VERBATIM
        )
    endif()
    add_test(NAME uuml_core_tests COMMAND uuml_core_tests)
    qt_add_executable(uuml_canvas_tests
        tests/diagram_canvas_tests.cpp
    )
    set_target_properties(uuml_canvas_tests PROPERTIES WIN32_EXECUTABLE OFF)
    target_link_libraries(uuml_canvas_tests PRIVATE
        uuml_canvas
        Qt6::Test
    )
    if(WIN32)
        add_custom_command(TARGET uuml_canvas_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:uuml_canvas_tests>"
            COMMENT "Deploying the Qt canvas-test runtime"
            VERBATIM
        )
    endif()
    add_test(NAME uuml_canvas_tests COMMAND uuml_canvas_tests)
    add_test(NAME uuml_cli_validate
        COMMAND uuml validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.uuml)
    add_test(NAME uuml_performance_example_validate
        COMMAND uuml validate
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/performance.uuml)
    add_test(NAME uuml_ui_smoke COMMAND uuml --smoke-test)
    if(WIN32)
        set_tests_properties(uuml_canvas_tests uuml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
    else()
        set_tests_properties(uuml_canvas_tests uuml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()
