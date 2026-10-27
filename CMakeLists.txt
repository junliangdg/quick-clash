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
add_library(quickclash_config src/infra/config/config_preflight.cpp src/infra/config/config_preflight.h
    src/infra/config/config_metadata.cpp src/infra/config/config_metadata.h
    src/infra/config/config_provider_stream.cpp
    src/infra/config/config_schema.cpp src/infra/config/config_schema.h
    src/infra/config/geo_resources.cpp src/infra/config/geo_resources.h)
string(JSON QUICKCLASH_GEOIP_SHA256 GET "${QUICKCLASH_RELEASE_MATRIX}" geo geoip sha256)
string(JSON QUICKCLASH_GEOSITE_SHA256 GET "${QUICKCLASH_RELEASE_MATRIX}" geo geosite sha256)
target_compile_definitions(quickclash_config PRIVATE
    QUICKCLASH_GEOIP_SHA256="${QUICKCLASH_GEOIP_SHA256}"
    QUICKCLASH_GEOSITE_SHA256="${QUICKCLASH_GEOSITE_SHA256}")
if(BUILD_TESTING)
    set_property(SOURCE src/infra/config/geo_resources.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        QUICKCLASH_DEVELOPMENT_GEO="${CMAKE_SOURCE_DIR}/.cache/geo")
endif()
target_link_libraries(quickclash_config PUBLIC quickclash_domain PRIVATE yaml-cpp::yaml-cpp)
quickclash_warnings(quickclash_config)
add_library(quickclash_storage
    src/infra/storage/json_store.cpp src/infra/storage/json_store.h
    src/infra/storage/geo_resource_writer.cpp src/infra/storage/geo_resource_writer.h
    src/infra/storage/provider_stage.cpp src/infra/storage/provider_stage.h
    src/infra/storage/config_repository.cpp src/infra/storage/config_repository.h
    src/infra/storage/settings_repository.cpp src/infra/storage/settings_repository.h
    src/platform/windows/credential_store.cpp src/platform/windows/credential_store.h)
target_link_libraries(quickclash_storage PUBLIC quickclash_application)
if(WIN32)
    target_link_libraries(quickclash_storage PRIVATE advapi32 crypt32)
endif()
quickclash_warnings(quickclash_storage)
set_target_properties(quickclash_storage PROPERTIES AUTOMOC ON)
if(QUICKCLASH_BUILD_UI)
find_package(Qt6 ${QUICKCLASH_MINIMUM_QT_VERSION} REQUIRED COMPONENTS Network)
add_library(quickclash_engine
    src/platform/windows/owned_process.cpp src/platform/windows/owned_process.h
    src/infra/core/service_client.cpp src/infra/core/service_client.h
    src/infra/core/pipe_http.cpp src/infra/core/pipe_http.h
    src/infra/core/engine_session.cpp src/infra/core/engine_session.h)
set_target_properties(quickclash_engine PROPERTIES AUTOMOC ON)
target_link_libraries(quickclash_engine PUBLIC quickclash_storage Qt6::Network)
if(WIN32)
    target_link_libraries(quickclash_engine PRIVATE iphlpapi ws2_32 userenv advapi32)
endif()
quickclash_warnings(quickclash_engine)
if(WIN32)
    add_executable(quickclash_service app/service_main.cpp
        src/platform/windows/service_host.cpp src/platform/windows/service_host.h)
    set_target_properties(quickclash_service PROPERTIES AUTOMOC ON)
    target_link_libraries(quickclash_service PRIVATE quickclash_engine quickclash_config yaml-cpp::yaml-cpp iphlpapi advapi32 psapi)
    quickclash_warnings(quickclash_service)
    install(TARGETS quickclash_service RUNTIME DESTINATION bin)
endif()
add_library(quickclash_runtime
    src/infra/storage/latest_mailbox.cpp src/infra/storage/latest_mailbox.h
    src/application/runtime_update.h
    src/infra/storage/settings_service.cpp src/infra/storage/settings_service.h
    src/platform/windows/system_proxy.cpp src/platform/windows/system_proxy.h
    src/application/desktop_runtime.cpp src/application/desktop_runtime.h)
target_sources(quickclash_runtime PRIVATE src/application/desktop_features.cpp src/application/desktop_providers.cpp src/application/desktop_backup.cpp src/application/desktop_resources.cpp)
set_target_properties(quickclash_runtime PROPERTIES AUTOMOC ON)
target_link_libraries(quickclash_runtime PUBLIC quickclash_engine quickclash_config)
if(WIN32)
    target_link_libraries(quickclash_runtime PRIVATE wininet psapi)
endif()
if(BUILD_TESTING)
    set_property(SOURCE src/application/desktop_runtime.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        QUICKCLASH_DEVELOPMENT_CORE="${CMAKE_SOURCE_DIR}/.cache/mihomo/mihomo.exe")
endif()
quickclash_warnings(quickclash_runtime)
endif()
if(QUICKCLASH_BUILD_UI)
    find_package(Qt6 ${QUICKCLASH_MINIMUM_QT_VERSION} REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Widgets Network LinguistTools)
    add_library(quickclash_localization STATIC src/presentation/localization.cpp src/presentation/localization.h)
    target_include_directories(quickclash_localization PUBLIC src)
    target_link_libraries(quickclash_localization PUBLIC Qt6::Core)
    quickclash_warnings(quickclash_localization)
    set(quickclash_catalog "${CMAKE_CURRENT_BINARY_DIR}/quickclash_zh_cn.qm")
    add_custom_command(OUTPUT "${quickclash_catalog}"
        COMMAND Qt6::lrelease "${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n/quickclash_zh_cn.ts" -qm "${quickclash_catalog}"
        DEPENDS resources/i18n/quickclash_zh_cn.ts VERBATIM)
    set_source_files_properties("${quickclash_catalog}" PROPERTIES GENERATED TRUE QT_RESOURCE_ALIAS quickclash_zh_cn.qm)
    # QtCore is a framework on macOS and lives under lib/ on Linux. Query Qt
    # itself instead of deriving the installation prefix from its library path.
    get_target_property(quickclash_qmake Qt6::qmake IMPORTED_LOCATION)
    execute_process(COMMAND "${quickclash_qmake}" -query QT_INSTALL_TRANSLATIONS
        OUTPUT_VARIABLE quickclash_qt_translations OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set_source_files_properties("${quickclash_qt_translations}/qtbase_zh_CN.qm" PROPERTIES QT_RESOURCE_ALIAS qtbase_zh_cn.qm)
    set_source_files_properties("${quickclash_qt_translations}/qtdeclarative_zh_CN.qm" PROPERTIES QT_RESOURCE_ALIAS qtdeclarative_zh_cn.qm)
    qt_add_resources(quickclash_localization quickclash_translations PREFIX /i18n FILES
        "${quickclash_catalog}"
        "${quickclash_qt_translations}/qtbase_zh_CN.qm"
        "${quickclash_qt_translations}/qtdeclarative_zh_CN.qm")
    qt_standard_project_setup(REQUIRES ${QUICKCLASH_MINIMUM_QT_VERSION})
    # The UI uses QML, not Designer forms. Avoid treating the handwritten ui_state.h
    # as a uic-generated header in the application and subsequent test targets.
    set(CMAKE_AUTOUIC OFF)
    qt_add_executable(quickclash app/main.cpp app/application_host.cpp app/application_host.h
        app/deferred_ui_unload.cpp app/deferred_ui_unload.h
        app/system_events.cpp app/system_events.h
        app/single_instance.cpp app/single_instance.h src/presentation/feature_registry.cpp)
    target_link_libraries(quickclash PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::Network quickclash_application quickclash_runtime quickclash_localization)
    if(WIN32)
        target_link_libraries(quickclash PRIVATE iphlpapi ws2_32)
    endif()
    target_include_directories(quickclash PRIVATE src/presentation)
    quickclash_warnings(quickclash)
    set_target_properties(quickclash PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME QuickClash)
    # Keep filesystem names snake_case; export explicit QML type names.
    set(QUICKCLASH_QML_FILES
        qml/main.qml
        qml/components/flat_tile.qml
        qml/components/theme_tokens.qml
        qml/components/action_button.qml
        qml/components/state_surface.qml
        qml/components/focus_ring.qml
        qml/components/themed_field.qml
        qml/components/themed_text_area.qml
        qml/components/themed_switch.qml
        qml/components/themed_check_box.qml
        qml/components/themed_combo_box.qml
        qml/components/themed_spin_box.qml
        qml/components/themed_dialog.qml
        qml/components/themed_tool_tip.qml
        qml/components/themed_progress.qml
        qml/components/deferred_content.qml
        qml/components/replay_list.qml
        qml/components/runtime_list.qml
        qml/components/subscription_details.qml
        qml/components/subscription_card.qml
        qml/components/outbound_status.qml
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
                src/presentation/profile_model.cpp src/presentation/profile_model.h
                src/presentation/runtime_list_model.cpp src/presentation/runtime_list_model.h
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
    if(WIN32 AND MSVC)
        # Portable packages carry the app-local CRT DLLs, not vc_redist.exe.
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
        include(InstallRequiredSystemLibraries)
    endif()
    qt_generate_deploy_qml_app_script(TARGET quickclash OUTPUT_SCRIPT quickclash_deploy
        NO_COMPILER_RUNTIME NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${quickclash_deploy})
endif()
if(BUILD_TESTING)
    add_executable(provider_stream_test tests/unit/provider_stream_test.cpp)
    target_link_libraries(provider_stream_test PRIVATE quickclash_storage quickclash_config)
    quickclash_warnings(provider_stream_test)
    add_test(NAME provider_stream COMMAND provider_stream_test)
    set_tests_properties(provider_stream PROPERTIES TIMEOUT 30)
    add_executable(bounded_read_test tests/unit/bounded_read_test.cpp)
    target_link_libraries(bounded_read_test PRIVATE quickclash_domain)
    quickclash_warnings(bounded_read_test)
    add_test(NAME bounded_read COMMAND bounded_read_test)
    set_tests_properties(bounded_read PROPERTIES TIMEOUT 15)
    add_executable(geo_resource_writer_test tests/unit/geo_resource_writer_test.cpp)
    target_link_libraries(geo_resource_writer_test PRIVATE quickclash_storage)
    quickclash_warnings(geo_resource_writer_test)
    add_test(NAME geo_resource_writer COMMAND geo_resource_writer_test)
    set_tests_properties(geo_resource_writer PROPERTIES TIMEOUT 30)
    add_executable(storage_test tests/unit/storage_test.cpp)
    target_link_libraries(storage_test PRIVATE quickclash_storage)
    quickclash_warnings(storage_test)
    add_test(NAME storage COMMAND storage_test)
    set_tests_properties(storage PROPERTIES TIMEOUT 15)
    add_executable(contracts_test tests/unit/contracts_test.cpp)
    target_link_libraries(contracts_test PRIVATE quickclash_application quickclash_config)
    target_compile_definitions(contracts_test PRIVATE QUICKCLASH_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    quickclash_warnings(contracts_test)
    add_test(NAME contracts COMMAND contracts_test)
    set_tests_properties(contracts PROPERTIES TIMEOUT 30)
    if(QUICKCLASH_BUILD_UI)
        add_executable(runtime_test tests/integration/runtime_test.cpp
            tests/integration/subscription_probe.cpp tests/integration/subscription_probe.h
            tests/integration/subscription_memory_probe.cpp tests/integration/subscription_memory_probe.h
            tests/integration/native_proxy_client.cpp tests/integration/native_proxy_client.h
            tests/integration/network_fixture.cpp tests/integration/network_fixture.h)
        if(WIN32)
            target_link_libraries(runtime_test PRIVATE wininet)
        endif()
        add_executable(proxy_lease_test tests/unit/proxy_lease_test.cpp)
        target_link_libraries(proxy_lease_test PRIVATE quickclash_runtime)
        quickclash_warnings(proxy_lease_test)
        add_test(NAME proxy_lease COMMAND proxy_lease_test)
        set_tests_properties(proxy_lease PROPERTIES TIMEOUT 15)
        target_link_libraries(runtime_test PRIVATE quickclash_runtime yaml-cpp::yaml-cpp)
        quickclash_warnings(runtime_test)
        add_test(NAME runtime COMMAND runtime_test)
        set_tests_properties(runtime PROPERTIES TIMEOUT 240 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        if(WIN32)
            add_executable(recovery_test tests/integration/recovery_test.cpp)
            target_link_libraries(recovery_test PRIVATE quickclash_runtime)
            quickclash_warnings(recovery_test)
            add_test(NAME recovery COMMAND recovery_test)
            set_tests_properties(recovery PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
        endif()
        find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
        add_executable(deferred_ui_unload_test tests/unit/deferred_ui_unload_test.cpp
            app/deferred_ui_unload.cpp app/deferred_ui_unload.h)
        target_include_directories(deferred_ui_unload_test PRIVATE app)
        target_link_libraries(deferred_ui_unload_test PRIVATE Qt6::Core Qt6::Test)
        quickclash_warnings(deferred_ui_unload_test)
        add_test(NAME deferred_ui_unload COMMAND deferred_ui_unload_test)
        set_tests_properties(deferred_ui_unload PROPERTIES TIMEOUT 15)
        add_executable(profile_model_test tests/unit/profile_model_test.cpp
            src/presentation/profile_model.cpp src/presentation/profile_model.h)
        target_link_libraries(profile_model_test PRIVATE quickclash_application Qt6::Qml Qt6::Test)
        quickclash_warnings(profile_model_test)
        add_test(NAME profile_model COMMAND profile_model_test)
        set_tests_properties(profile_model PROPERTIES TIMEOUT 15)
        add_executable(latest_mailbox_test tests/unit/latest_mailbox_test.cpp)
        target_link_libraries(latest_mailbox_test PRIVATE quickclash_runtime Qt6::Test)
        quickclash_warnings(latest_mailbox_test)
        add_test(NAME latest_mailbox COMMAND latest_mailbox_test)
        set_tests_properties(latest_mailbox PROPERTIES TIMEOUT 15)
        add_executable(runtime_publication_test tests/unit/runtime_publication_test.cpp)
        target_link_libraries(runtime_publication_test PRIVATE quickclash_runtime Qt6::Test)
        quickclash_warnings(runtime_publication_test)
        add_test(NAME runtime_publication COMMAND runtime_publication_test)
        set_tests_properties(runtime_publication PROPERTIES TIMEOUT 60)
        add_executable(runtime_list_model_test tests/unit/runtime_list_model_test.cpp
            src/presentation/runtime_list_model.cpp src/presentation/runtime_list_model.h)
        target_link_libraries(runtime_list_model_test PRIVATE quickclash_application Qt6::Qml Qt6::Test)
        quickclash_warnings(runtime_list_model_test)
        add_test(NAME runtime_list_model COMMAND runtime_list_model_test)
        set_tests_properties(runtime_list_model PROPERTIES TIMEOUT 15)
        add_executable(localization_test tests/unit/localization_test.cpp)
        target_link_libraries(localization_test PRIVATE quickclash_localization)
        quickclash_warnings(localization_test)
        add_test(NAME localization COMMAND localization_test)
        # Keep test-only include paths/definitions out of the application and its
        # generated QML objects so a tested build can reuse them for packaging.
        add_library(quickclash_test_probes STATIC app/host_probe.cpp app/host_probe.h app/keyboard_probe.cpp
            tests/integration/runtime_ui_probe.cpp tests/integration/runtime_ui_probe.h
            tests/integration/subscription_ui_probe.cpp tests/integration/subscription_ui_probe.h
            tests/integration/subscription_memory_probe.cpp tests/integration/network_fixture.cpp
            tests/integration/visual_ui_probe.cpp)
        target_include_directories(quickclash_test_probes PRIVATE app tests/integration src/presentation)
        target_link_libraries(quickclash_test_probes PRIVATE quickclash_runtime Qt6::Quick Qt6::Widgets Qt6::Test)
        target_compile_definitions(quickclash_test_probes PRIVATE QUICKCLASH_TEST_PROBES)
        quickclash_warnings(quickclash_test_probes)
        target_link_libraries(quickclash PRIVATE quickclash_test_probes)
        set_property(SOURCE app/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS QUICKCLASH_TEST_PROBES)
        set_property(SOURCE app/main.cpp APPEND PROPERTY INCLUDE_DIRECTORIES
            "${CMAKE_SOURCE_DIR}/tests/integration")
        add_executable(qt_cost_probe bench/qt_cost_probe.cpp)
        target_link_libraries(qt_cost_probe PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Widgets)
        quickclash_warnings(qt_cost_probe)
        add_executable(ui_state_test tests/unit/ui_state_test.cpp
            src/presentation/profile_model.cpp src/presentation/profile_model.h
            src/presentation/runtime_list_model.cpp src/presentation/runtime_list_model.h
            src/presentation/feature_registry.cpp src/presentation/ui_state.cpp src/presentation/ui_state.h
            src/presentation/replay_model.cpp src/presentation/replay_model.h)
        target_link_libraries(ui_state_test PRIVATE quickclash_application quickclash_runtime Qt6::Qml)
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
        add_test(NAME ui_visual COMMAND quickclash --visual-test)
        set_tests_properties(ui_visual PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;binding loop;Binding loop;Required property;Error loading QML"
            TIMEOUT 30)
        add_test(NAME ui_responsive COMMAND quickclash --responsive-test)
        add_test(NAME ui_runtime_lists COMMAND quickclash --runtime-list-test)
        add_test(NAME ui_subscriptions COMMAND quickclash --subscription-ui-test)
        add_test(NAME ui_subscriptions_zh COMMAND quickclash --subscription-ui-test --probe-language=zh-CN)
        set_tests_properties(ui_subscriptions ui_subscriptions_zh PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;binding loop;Binding loop;Required property;Error loading QML"
            TIMEOUT 15)
        set_tests_properties(ui_runtime_lists PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;binding loop;Binding loop;Required property;Error loading QML"
            TIMEOUT 15)
        set_tests_properties(ui_responsive PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1"
            FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;binding loop;Binding loop;Error loading QML"
            TIMEOUT 15)
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
    # Daily CI keeps all fast tests, including UI lifecycle regressions. Slow
    # integration tests and manual probes remain available in the full build.
    if(QUICKCLASH_BUILD_UI)
        set_tests_properties(runtime runtime_publication PROPERTIES LABELS ci_slow)
        if(WIN32)
            set_tests_properties(recovery PROPERTIES LABELS ci_slow)
        endif()
    endif()
    set(quickclash_full_only_targets runtime_test runtime_publication_test recovery_test
        qt_cost_probe core_job_probe core_isolation_probe)
    get_property(quickclash_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(quickclash_fast_targets)
    foreach(quickclash_target IN LISTS quickclash_directory_targets)
        get_target_property(quickclash_target_type ${quickclash_target} TYPE)
        if(quickclash_target_type STREQUAL "EXECUTABLE" AND
            NOT quickclash_target IN_LIST quickclash_full_only_targets)
            list(APPEND quickclash_fast_targets ${quickclash_target})
        endif()
    endforeach()
    add_custom_target(quickclash_ci_fast DEPENDS ${quickclash_fast_targets})
endif()
