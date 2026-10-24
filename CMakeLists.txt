cmake_minimum_required(VERSION 3.24)
project(quickclash VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(QUICKCLASH_BUILD_UI "Build the desktop Quick shell" ON)
option(QUICKCLASH_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
include(CTest)
include(cmake/project_warnings.cmake)
set(QUICKCLASH_MINIMUM_QT_VERSION 6.8)
find_package(Qt6 ${QUICKCLASH_MINIMUM_QT_VERSION} REQUIRED COMPONENTS Core)
include(FetchContent)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
file(READ "${CMAKE_SOURCE_DIR}/docs/baseline/release_matrix.json" QUICKCLASH_RELEASE_MATRIX)
string(JSON QUICKCLASH_YAML_CACHE_FILE GET "${QUICKCLASH_RELEASE_MATRIX}" toolchain yaml_cpp cache_file)
string(JSON QUICKCLASH_YAML_SHA256 GET "${QUICKCLASH_RELEASE_MATRIX}" toolchain yaml_cpp sha256)
set(QUICKCLASH_YAML_ARCHIVE "${CMAKE_SOURCE_DIR}/.cache/${QUICKCLASH_YAML_CACHE_FILE}")
if(NOT EXISTS "${QUICKCLASH_YAML_ARCHIVE}")
    message(FATAL_ERROR "Run scripts/fetch_dependencies.ps1 first.")
endif()
FetchContent_Declare(yaml_cpp URL "${QUICKCLASH_YAML_ARCHIVE}"
    URL_HASH SHA256=${QUICKCLASH_YAML_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(yaml_cpp)
add_library(quickclash_domain src/domain/contracts.h src/domain/operation.cpp src/domain/operation.h)
target_include_directories(quickclash_domain PUBLIC src)
target_link_libraries(quickclash_domain PUBLIC Qt6::Core)
quickclash_warnings(quickclash_domain)
add_library(quickclash_application INTERFACE)
target_include_directories(quickclash_application INTERFACE src)
target_link_libraries(quickclash_application INTERFACE quickclash_domain)
add_library(quickclash_config src/infra/config/config_preflight.cpp src/infra/config/config_preflight.h)
target_link_libraries(quickclash_config PUBLIC quickclash_domain PRIVATE yaml-cpp::yaml-cpp)
quickclash_warnings(quickclash_config)
if(QUICKCLASH_BUILD_UI)
    find_package(Qt6 ${QUICKCLASH_MINIMUM_QT_VERSION} REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Widgets Network)
    qt_standard_project_setup(REQUIRES ${QUICKCLASH_MINIMUM_QT_VERSION})
    # The UI uses QML, not Designer forms. Avoid treating the handwritten ui_state.h
    # as a uic-generated header in the application and subsequent test targets.
    set(CMAKE_AUTOUIC OFF)
    qt_add_executable(quickclash app/main.cpp app/application_host.cpp app/application_host.h
        app/host_probe.cpp app/host_probe.h
        app/single_instance.cpp app/single_instance.h src/presentation/feature_registry.cpp)
    target_link_libraries(quickclash PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Network quickclash_application)
    target_include_directories(quickclash PRIVATE src/presentation)
    quickclash_warnings(quickclash)
    set_target_properties(quickclash PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME QuickClash)
    # Keep filesystem names snake_case; export explicit QML type names.
    set(QUICKCLASH_QML_FILES
        qml/main.qml
        qml/components/flat_tile.qml
        qml/components/theme_tokens.qml
        qml/components/action_button.qml
        qml/components/deferred_content.qml
        qml/components/replay_list.qml
        qml/components/traffic_sparkline.qml
        qml/shell/page_host.qml
        qml/shell/embedded_host.qml
        qml/features/home/home_page.qml
        qml/features/proxies/proxies_page.qml
        qml/features/proxies/proxy_summary.qml
        qml/features/subscriptions/subscriptions_page.qml
        qml/features/connections/connections_page.qml
        qml/features/rules/rules_page.qml
        qml/features/logs/logs_page.qml
        qml/features/tests/tests_page.qml
        qml/features/settings/settings_page.qml)
    foreach(qml_file IN LISTS QUICKCLASH_QML_FILES)
        get_filename_component(qml_stem "${qml_file}" NAME_WE)
        string(REPLACE "_" ";" qml_words "${qml_stem}")
        set(qml_type "")
        foreach(qml_word IN LISTS qml_words)
            string(SUBSTRING "${qml_word}" 0 1 qml_initial)
            string(TOUPPER "${qml_initial}" qml_initial)
            string(SUBSTRING "${qml_word}" 1 -1 qml_tail)
            string(APPEND qml_type "${qml_initial}${qml_tail}")
        endforeach()
        string(REGEX REPLACE "^qml/" "" qml_alias "${qml_file}")
        set_source_files_properties("${qml_file}" PROPERTIES
            QT_QML_SOURCE_TYPENAME "${qml_type}" QT_RESOURCE_ALIAS "${qml_alias}")
    endforeach()
    qt_add_qml_module(quickclash URI QuickClash.Ui VERSION 1.0
        RESOURCE_PREFIX /qt/qml
        QML_FILES ${QUICKCLASH_QML_FILES}
        SOURCES src/presentation/ui_state.cpp src/presentation/ui_state.h
                src/presentation/replay_model.cpp src/presentation/replay_model.h)
    if(MSVC)
        # Qt 6.8 qmlcachegen instantiations trigger C4702 with optimized MSVC.
        # Exempt only generated QML cache sources; keep /W4 and /WX elsewhere.
        get_target_property(quickclash_sources quickclash SOURCES)
        foreach(qml_cache_source IN LISTS quickclash_sources)
            if(qml_cache_source MATCHES "/\\.rcc/qmlcache/.*\\.cpp$")
                set_property(SOURCE "${qml_cache_source}" APPEND PROPERTY COMPILE_OPTIONS /wd4702)
            endif()
        endforeach()
    endif()
    install(TARGETS quickclash BUNDLE DESTINATION . RUNTIME DESTINATION bin)
    qt_generate_deploy_qml_app_script(TARGET quickclash OUTPUT_SCRIPT quickclash_deploy NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${quickclash_deploy})
endif()
if(BUILD_TESTING)
    add_executable(contracts_test tests/unit/contracts_test.cpp)
    target_link_libraries(contracts_test PRIVATE quickclash_application quickclash_config)
    target_compile_definitions(contracts_test PRIVATE QUICKCLASH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    quickclash_warnings(contracts_test)
    add_test(NAME contracts COMMAND contracts_test)
    set_tests_properties(contracts PROPERTIES TIMEOUT 30)
    if(QUICKCLASH_BUILD_UI)
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
        target_sources(quickclash PRIVATE app/keyboard_probe.cpp)
        target_link_libraries(quickclash PRIVATE Qt6::Test)
        target_compile_definitions(quickclash PRIVATE QUICKCLASH_TEST_PROBES)
        add_executable(qt_cost_probe bench/qt_cost_probe.cpp)
        target_link_libraries(qt_cost_probe PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
        quickclash_warnings(qt_cost_probe)
        add_executable(ui_state_test tests/unit/ui_state_test.cpp
            src/presentation/feature_registry.cpp src/presentation/ui_state.cpp src/presentation/ui_state.h
            src/presentation/replay_model.cpp src/presentation/replay_model.h)
        target_link_libraries(ui_state_test PRIVATE quickclash_application Qt6::Qml)
        quickclash_warnings(ui_state_test)
        add_test(NAME ui_state COMMAND ui_state_test)
        set_tests_properties(ui_state PROPERTIES TIMEOUT 30)
        add_executable(single_instance_test tests/unit/single_instance_test.cpp
            app/single_instance.cpp app/single_instance.h)
        target_include_directories(single_instance_test PRIVATE app)
        target_link_libraries(single_instance_test PRIVATE Qt6::Core Qt6::Network)
        quickclash_warnings(single_instance_test)
        add_test(NAME single_instance COMMAND single_instance_test)
        set_tests_properties(single_instance PROPERTIES TIMEOUT 15)
        add_test(NAME ui_prototype COMMAND quickclash --prototype-test)
        set_tests_properties(ui_prototype PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;is not a type;is not installed;Required property;Error loading QML"
            TIMEOUT 60)
        add_test(NAME qml_smoke COMMAND quickclash --smoke-test)
        add_test(NAME ui_keyboard COMMAND quickclash --keyboard-test)
        set_tests_properties(ui_keyboard PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            TIMEOUT 15)
        add_test(NAME host_lifecycle COMMAND quickclash --host-test)
        set_tests_properties(host_lifecycle PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            TIMEOUT 15)
        set_tests_properties(qml_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;is not a type;is not installed;Required property;Error loading QML"
            TIMEOUT 15)
    endif()
    if(WIN32)
        add_executable(core_job_probe tests/integration/core_job_probe.cpp)
        quickclash_warnings(core_job_probe)
        add_executable(core_isolation_probe tests/integration/core_isolation_probe.cpp)
        target_link_libraries(core_isolation_probe PRIVATE userenv advapi32 ws2_32)
        quickclash_warnings(core_isolation_probe)
    endif()
endif()
