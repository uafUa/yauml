cmake_minimum_required(VERSION 3.24)

project(yauml VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Concurrent Core Gui Network Qml Quick QuickControls2 Test)
qt_standard_project_setup(REQUIRES 6.8)

# C++ import is built around Clang's stable C API. Keep the dependency optional
# so the diagram editor remains buildable on machines without an LLVM SDK; the
# import service then reports a precise runtime diagnostic instead of silently
# falling back to an incomplete handwritten parser.
set(YAUML_LLVM_HINTS
    "$ENV{LLVM_ROOT}"
    "$ENV{LLVM_HOME}"
    "C:/Program Files/LLVM"
    "/usr/local/opt/llvm"
    "/opt/homebrew/opt/llvm"
)
find_path(YAUML_LIBCLANG_INCLUDE_DIR clang-c/Index.h
    HINTS ${YAUML_LLVM_HINTS}
    PATH_SUFFIXES include)
find_library(YAUML_LIBCLANG_LIBRARY NAMES libclang clang
    HINTS ${YAUML_LLVM_HINTS}
    PATH_SUFFIXES lib)
option(YAUML_REQUIRE_LIBCLANG
    "Fail configuration when the C++ import dependency is unavailable" OFF)

add_library(yauml_theme STATIC
    src/ui/ui_theme.cpp
    src/ui/ui_theme.h
)
target_include_directories(yauml_theme PUBLIC src)
target_link_libraries(yauml_theme PUBLIC Qt6::Core Qt6::Gui)

if(MSVC)
    target_compile_options(yauml_theme PRIVATE /W4 /permissive-)
else()
    target_compile_options(yauml_theme PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(yauml_core STATIC
    src/core/application_settings.cpp
    src/core/application_settings.h
    src/core/connector_port_layout.cpp
    src/core/connector_port_layout.h
    src/core/cpp_import.cpp
    src/core/cpp_import.h
    src/core/cpp_import_matching.cpp
    src/core/cpp_import_matching.h
    src/core/cpp_import_controller.cpp
    src/core/cpp_import_controller.h
    src/core/diagnostic_model.cpp
    src/core/diagnostic_model.h
    src/core/diagram_filter.cpp
    src/core/diagram_filter.h
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
    src/core/project_serializer_test_support.cpp
    src/core/project_serializer_test_support.h
    src/core/project_style.cpp
    src/core/project_style.h
    src/core/stereotype_catalog.cpp
    src/core/stereotype_catalog.h
    src/core/update_manifest.cpp
    src/core/update_manifest.h
    src/core/project_tree_model.cpp
    src/core/project_tree_model.h
    src/core/workspace_controller.cpp
    src/core/workspace_controller.h
)

target_include_directories(yauml_core PUBLIC src)
target_link_libraries(yauml_core PUBLIC
    yauml_theme
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
)
if(YAUML_LIBCLANG_INCLUDE_DIR AND YAUML_LIBCLANG_LIBRARY)
    target_compile_definitions(yauml_core PRIVATE YAUML_HAS_LIBCLANG=1)
    target_include_directories(yauml_core PRIVATE ${YAUML_LIBCLANG_INCLUDE_DIR})
    target_link_libraries(yauml_core PUBLIC ${YAUML_LIBCLANG_LIBRARY})
    message(STATUS "C++ import enabled with ${YAUML_LIBCLANG_LIBRARY}")
elseif(YAUML_REQUIRE_LIBCLANG)
    message(FATAL_ERROR
        "libclang was not found, but YAUML_REQUIRE_LIBCLANG is enabled")
else()
    target_compile_definitions(yauml_core PRIVATE YAUML_HAS_LIBCLANG=0)
    message(WARNING
        "libclang was not found; C++ import will report that it is unavailable")
endif()

add_library(yauml_ui_support STATIC
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
set_target_properties(yauml_ui_support PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
)
target_include_directories(yauml_ui_support PUBLIC src)
target_link_libraries(yauml_ui_support PUBLIC Qt6::Core)

add_library(yauml_canvas STATIC
    src/ui/diagram_clipping.cpp
    src/ui/diagram_clipping.h
    src/ui/diagram_canvas.cpp
    src/ui/diagram_canvas.h
)
target_include_directories(yauml_canvas PUBLIC src)
target_link_libraries(yauml_canvas PUBLIC
    yauml_core
    yauml_ui_support
    Qt6::Gui
    Qt6::Quick
)

if(MSVC)
    target_compile_options(yauml_canvas PRIVATE /W4 /permissive-)
else()
    target_compile_options(yauml_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MSVC)
    target_compile_options(yauml_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(yauml_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

qt_add_executable(yauml_generate_performance_example
    tools/generate_performance_example.cpp
)
target_link_libraries(yauml_generate_performance_example PRIVATE yauml_core Qt6::Core)

add_custom_target(generate_performance_example
    COMMAND yauml_generate_performance_example
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/performance.yauml" 600
    DEPENDS yauml_generate_performance_example
    COMMENT "Generating the 600-node performance example"
    VERBATIM)

qt_add_executable(yauml
    src/app/icon_registry.cpp
    src/app/icon_registry.h
    src/app/main.cpp
    src/app/source_folder_picker.cpp
    src/app/source_folder_picker.h
    src/app/update_controller.cpp
    src/app/update_controller.h
)

file(GLOB_RECURSE YAUML_ICON_SVG_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/*.svg")
qt_add_resources(yauml "yauml_icons"
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/action-icons.json5"
        ${YAUML_ICON_SVG_FILES}
)

if(WIN32 AND YAUML_LIBCLANG_LIBRARY)
    get_filename_component(YAUML_LLVM_LIBRARY_DIRECTORY
        "${YAUML_LIBCLANG_LIBRARY}" DIRECTORY)
    get_filename_component(YAUML_LLVM_ROOT
        "${YAUML_LLVM_LIBRARY_DIRECTORY}" DIRECTORY)
    find_file(YAUML_LIBCLANG_RUNTIME libclang.dll
        HINTS "${YAUML_LLVM_ROOT}/bin" ${YAUML_LLVM_HINTS}
        PATH_SUFFIXES bin)
endif()

qt_add_qml_module(yauml
    URI Yauml
    VERSION 1.0
    QML_FILES
        qml/CatalogAction.qml
        qml/CatalogButton.qml
        qml/CatalogDialog.qml
        qml/CatalogMenuItem.qml
        qml/CatalogToolButton.qml
        qml/CompartmentVisibilityToolButton.qml
        qml/ContextToolboxFrame.qml
        qml/DiagramArea.qml
        qml/DiagramCanvasMenu.qml
        qml/DiagramFilterDialog.qml
        qml/DiagramView.qml
        qml/DiagramWindow.qml
        qml/Main.qml
        qml/ProjectStereotypeDialog.qml
        qml/ProjectStyleDialog.qml
        qml/StereotypeDropdown.qml
        qml/StyleAssignmentMenu.qml
)

target_link_libraries(yauml PRIVATE
    yauml_canvas
    yauml_core
    yauml_ui_support
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
target_compile_definitions(yauml PRIVATE
    YAUML_VERSION="${PROJECT_VERSION}"
    YAUML_UPDATE_MANIFEST_URL="https://uafua.github.io/yauml/updates/windows/x64/stable/latest.json"
)
if(WIN32)
    target_link_libraries(yauml PRIVATE ole32 shell32)
endif()

if(MSVC)
    target_compile_options(yauml PRIVATE /W4 /permissive-)
else()
    target_compile_options(yauml PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    option(YAUML_DEPLOY_QT_RUNTIME
        "Copy the Qt runtime beside yauml.exe after linking" ON)
    if(YAUML_DEPLOY_QT_RUNTIME)
        get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(QT_RUNTIME_DIRECTORY
            "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${QT_RUNTIME_DIRECTORY}"
            REQUIRED)
        add_custom_command(TARGET yauml POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml"
                --no-translations
                --compiler-runtime
                "$<TARGET_FILE:yauml>"
            COMMENT "Deploying the Qt runtime beside yauml.exe"
            VERBATIM)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp_import"
        YAUML_CPP_IMPORT_FIXTURE_SOURCE_DIR)
    file(TO_CMAKE_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/cpp_import/types.cpp"
        YAUML_CPP_IMPORT_FIXTURE_SOURCE)
    set(YAUML_CPP_IMPORT_FIXTURE_BUILD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/cpp-import-fixture")
    file(MAKE_DIRECTORY "${YAUML_CPP_IMPORT_FIXTURE_BUILD_DIR}")
    file(GENERATE
        OUTPUT "${YAUML_CPP_IMPORT_FIXTURE_BUILD_DIR}/compile_commands.json"
        CONTENT
            "[{\"directory\":\"${YAUML_CPP_IMPORT_FIXTURE_SOURCE_DIR}\",\"file\":\"${YAUML_CPP_IMPORT_FIXTURE_SOURCE}\",\"arguments\":[\"clang++\",\"-std=c++20\",\"${YAUML_CPP_IMPORT_FIXTURE_SOURCE}\"]}]\n")
    qt_add_executable(yauml_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(yauml_core_tests PRIVATE
        yauml_core
        yauml_ui_support
        Qt6::Test
    )
    if(WIN32)
        add_custom_command(TARGET yauml_core_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:yauml_core_tests>"
            COMMENT "Deploying the Qt test runtime"
            VERBATIM
        )
    endif()
    add_test(NAME yauml_core_tests COMMAND yauml_core_tests)
    qt_add_executable(yauml_canvas_tests
        tests/diagram_canvas_tests.cpp
    )
    set_target_properties(yauml_canvas_tests PROPERTIES WIN32_EXECUTABLE OFF)
    target_link_libraries(yauml_canvas_tests PRIVATE
        yauml_canvas
        Qt6::Test
    )
    if(WIN32)
        add_custom_command(TARGET yauml_canvas_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:yauml_canvas_tests>"
            COMMENT "Deploying the Qt canvas-test runtime"
            VERBATIM
        )
    endif()
    add_test(NAME yauml_canvas_tests COMMAND yauml_canvas_tests)
    qt_add_executable(yauml_render_tests
        tests/render_regression_tests.cpp
    )
    set_target_properties(yauml_render_tests PROPERTIES WIN32_EXECUTABLE OFF)
    target_compile_definitions(yauml_render_tests PRIVATE
        YAUML_RENDER_BASELINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rendering"
        YAUML_RENDER_ARTIFACT_DIR="${CMAKE_CURRENT_BINARY_DIR}/visual-regression-artifacts"
    )
    target_link_libraries(yauml_render_tests PRIVATE
        yauml_canvas
        Qt6::Test
    )
    if(WIN32)
        add_custom_command(TARGET yauml_render_tests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>"
                "$<TARGET_FILE_DIR:yauml_render_tests>"
            COMMENT "Deploying the Qt render-test runtime"
            VERBATIM
        )
    endif()
    add_test(NAME yauml_render_regression COMMAND yauml_render_tests)
    add_test(NAME yauml_cli_validate
        COMMAND yauml validate ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.yauml)
    add_test(NAME yauml_performance_example_validate
        COMMAND yauml validate
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/performance.yauml)
    if(YAUML_LIBCLANG_INCLUDE_DIR AND YAUML_LIBCLANG_LIBRARY)
        add_test(NAME yauml_cpp_preview
            COMMAND yauml cpp-preview
                ${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.yauml
                ${YAUML_CPP_IMPORT_FIXTURE_BUILD_DIR})
    endif()
    add_test(NAME yauml_ui_smoke COMMAND yauml --smoke-test)
    add_test(NAME yauml_color_literal_guard
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_color_literals.cmake)
    if(WIN32)
        set(YAUML_RUNTIME_TESTS
            yauml_core_tests
            yauml_canvas_tests
            yauml_render_regression
            yauml_cli_validate
            yauml_performance_example_validate
            yauml_ui_smoke)
        if(YAUML_LIBCLANG_INCLUDE_DIR AND YAUML_LIBCLANG_LIBRARY)
            list(APPEND YAUML_RUNTIME_TESTS yauml_cpp_preview)
        endif()
        # A package build deliberately skips post-link deployment. Give CTest
        # the selected Qt kit explicitly so Windows never opens a missing-DLL
        # system dialog while running the pre-package test suite.
        set_tests_properties(${YAUML_RUNTIME_TESTS} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(yauml_canvas_tests yauml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_QUICK_BACKEND=software")
        set_tests_properties(yauml_render_regression PROPERTIES
            ENVIRONMENT
                "QT_QPA_PLATFORM=windows;QSG_RHI_PREFER_SOFTWARE_RENDERER=1;QT_SCALE_FACTOR=1")
    else()
        set_tests_properties(yauml_canvas_tests yauml_ui_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        set_tests_properties(yauml_render_regression PROPERTIES
            ENVIRONMENT
                "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=1")
    endif()
endif()

if(WIN32 AND YAUML_LIBCLANG_RUNTIME)
    foreach(YAUML_CLANG_RUNTIME_TARGET
            yauml yauml_core_tests yauml_canvas_tests yauml_render_tests)
        if(TARGET ${YAUML_CLANG_RUNTIME_TARGET})
            add_custom_command(TARGET ${YAUML_CLANG_RUNTIME_TARGET} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${YAUML_LIBCLANG_RUNTIME}"
                    "$<TARGET_FILE_DIR:${YAUML_CLANG_RUNTIME_TARGET}>"
                COMMENT "Deploying libclang for C++ import"
                VERBATIM)
        endif()
    endforeach()
endif()
