cmake_minimum_required(VERSION 3.24)

project(uuml VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Concurrent Core Gui Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.8)

# C++ import is built around Clang's stable C API. Keep the dependency optional
# so the diagram editor remains buildable on machines without an LLVM SDK; the
# import service then reports a precise runtime diagnostic instead of silently
# falling back to an incomplete handwritten parser.
set(UUML_LLVM_HINTS
    "$ENV{LLVM_ROOT}"
    "$ENV{LLVM_HOME}"
    "C:/Program Files/LLVM"
    "/usr/local/opt/llvm"
    "/opt/homebrew/opt/llvm"
)
find_path(UUML_LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${UUML_LLVM_HINTS}
    PATH_SUFFIXES include)
find_library(UUML_LIBCLANG_LIBRARY NAMES libclang clang
    HINTS ${UUML_LLVM_HINTS}
    PATH_SUFFIXES lib)
option(UUML_REQUIRE_LIBCLANG
    "Fail configuration when the C++ import dependency is unavailable" OFF)

add_library(uuml_theme STATIC
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
)
target_include_directories(uuml_theme PUBLIC src)
target_link_libraries(uuml_theme PUBLIC Qt6::Core Qt6::Gui)

if(MSVC)
    target_compile_options(uuml_theme PRIVATE /W4 /permissive-)
else()
    target_compile_options(uuml_theme PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(uuml_core STATIC
    src/core/application_settings.cpp
    src/core/application_settings.h
    src/core/connector_port_layout.cpp
    src/core/connector_port_layout.h
    src/core/cpp_import.cpp
    src/core/cpp_import.h
    src/core/cpp_import_controller.cpp
    src/core/cpp_import_controller.h
    src/core/diagnostic_model.cpp
    src/core/diagnostic_model.h
    src/core/json5.cpp
    src/core/json5.h
    src/core/presentation_layout.cpp
    src/core/presentation_layout.h
    src/core/project_command.cpp
    src/core/project_command.h
    src/core/project_commands.cpp
    src/core/project_commands.h
    src/core/project_controller.cpp
    src/core/project_controller.h
    src/core/project_data.cpp
    src/core/project_data.h
    src/core/project_schema.cpp
    src/core/project_schema.h
    src/core/project_schema_version.h
    src/core/project_serializer.cpp
    src/core/project_serializer.h
    src/core/project_style.cpp
    src/core/project_style.h
    src/core/project_tree_model.cpp
    src/core/project_tree_model.h
    src/core/workspace_controller.cpp
    src/core/workspace_controller.h
)

target_include_directories(uuml_core PUBLIC src)
target_link_libraries(uuml_core PUBLIC
    uuml_theme
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
)
if(UUML_LIBCLANG_INCLUDE_DIR AND UUML_LIBCLANG_LIBRARY)
    target_compile_definitions(uuml_core PRIVATE UUML_HAS_LIBCLANG=1)
    target_include_directories(uuml_core PRIVATE ${UUML_LIBCLANG_INCLUDE_DIR})
    target_link_libraries(uuml_core PUBLIC ${UUML_LIBCLANG_LIBRARY})
    message(STATUS "C++ import enabled with ${UUML_LIBCLANG_LIBRARY}")
elseif(UUML_REQUIRE_LIBCLANG)
    message(FATAL_ERROR
        "libclang was not found, but UUML_REQUIRE_LIBCLANG is enabled")
else()
    target_compile_definitions(uuml_core PRIVATE UUML_HAS_LIBCLANG=0)
    message(WARNING
        "libclang was not found; C++ import will report that it is unavailable")
endif()

add_library(uuml_ui_support STATIC
    src/ui/connector_routing.cpp
    src/ui/connector_routing.h
    src/ui/diagram_arrangement.cpp
    src/ui/diagram_arrangement.h
    src/ui/diagram_snapping.cpp
    src/ui/diagram_snapping.h
    src/ui/relationship_style.cpp
    src/ui/relationship_style.h
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
    src/ui/diagram_clipping.cpp
    src/ui/diagram_clipping.h
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
    src/app/icon_registry.cpp
    src/app/icon_registry.h
    src/app/main.cpp
)

file(GLOB_RECURSE UUML_ICON_SVG_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
qt_add_resources(uuml "uuml_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/action-icons.json5"
        ${UUML_ICON_SVG_FILES}
)

if(WIN32 AND UUML_LIBCLANG_LIBRARY)
    get_filename_component(UUML_LLVM_LIBRARY_DIRECTORY
        "${UUML_LIBCLANG_LIBRARY}" DIRECTORY)
    get_filename_component(UUML_LLVM_ROOT
        "${UUML_LLVM_LIBRARY_DIRECTORY}" DIRECTORY)
    find_file(UUML_LIBCLANG_RUNTIME libclang.dll
        HINTS "${UUML_LLVM_ROOT}/bin" ${UUML_LLVM_HINTS}
        PATH_SUFFIXES bin)
endif()

qt_add_qml_module(uuml
    URI Uuml
    VERSION 1.0
    QML_FILES
        qml/CatalogAction.qml
        qml/CatalogButton.qml
        qml/CatalogDialog.qml
        qml/CatalogMenuItem.qml
        qml/CatalogToolButton.qml
        qml/ContextToolboxFrame.qml
        qml/DiagramArea.qml
        qml/DiagramView.qml
        qml/DiagramWindow.qml
        qml/Main.qml
        qml/ProjectStyleDialog.qml
        qml/StyleAssignmentMenu.qml
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
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp_import"
        UUML_CPP_IMPORT_FIXTURE_SOURCE_DIR)
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp_import/types.cpp"
        UUML_CPP_IMPORT_FIXTURE_SOURCE)
    set(UUML_CPP_IMPORT_FIXTURE_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/cpp-import-fixture")
    file(MAKE_DIRECTORY "${UUML_CPP_IMPORT_FIXTURE_BUILD_DIR}")
    file(GENERATE
        OUTPUT "${UUML_CPP_IMPORT_FIXTURE_BUILD_DIR}/compile_commands.json"
        CONTENT
            "[{\"directory\":\"${UUML_CPP_IMPORT_FIXTURE_SOURCE_DIR}\",\"file\":\"${UUML_CPP_IMPORT_FIXTURE_SOURCE}\",\"arguments\":[\"clang++\",\"-std=c++20\",\"${UUML_CPP_IMPORT_FIXTURE_SOURCE}\"]}]\n")
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
    if(UUML_LIBCLANG_INCLUDE_DIR AND UUML_LIBCLANG_LIBRARY)
        add_test(NAME uuml_cpp_preview
            COMMAND uuml cpp-preview
                ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.uuml
                ${UUML_CPP_IMPORT_FIXTURE_BUILD_DIR})
    endif()
    add_test(NAME uuml_ui_smoke COMMAND uuml --smoke-test)
    add_test(NAME uuml_color_literal_guard
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_color_literals.cmake)
    if(WIN32)
        set(UUML_RUNTIME_TESTS
            uuml_core_tests
            uuml_canvas_tests
            uuml_cli_validate
            uuml_performance_example_validate
            uuml_ui_smoke)
        if(UUML_LIBCLANG_INCLUDE_DIR AND UUML_LIBCLANG_LIBRARY)
            list(APPEND UUML_RUNTIME_TESTS uuml_cpp_preview)
        endif()
        # A package build deliberately skips post-link deployment. Give CTest
        # the selected Qt kit explicitly so Windows never opens a missing-DLL
        # system dialog while running the pre-package test suite.
        set_tests_properties(${UUML_RUNTIME_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(uuml_canvas_tests uuml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
    else()
        set_tests_properties(uuml_canvas_tests uuml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()

if(WIN32 AND UUML_LIBCLANG_RUNTIME)
    foreach(UUML_CLANG_RUNTIME_TARGET
            uuml uuml_core_tests uuml_canvas_tests)
        if(TARGET ${UUML_CLANG_RUNTIME_TARGET})
            add_custom_command(TARGET ${UUML_CLANG_RUNTIME_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${UUML_LIBCLANG_RUNTIME}"
                    "$<TARGET_FILE_DIR:${UUML_CLANG_RUNTIME_TARGET}>"
                COMMENT "Deploying libclang for C++ import"
                VERBATIM)
        endif()
    endforeach()
endif()
