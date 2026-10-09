cmake_minimum_required(VERSION 3.14)
project(linkctl-daemon C)

set(CMAKE_C_STANDARD 11)

# Prefer Homebrew paths on macOS
if(APPLE)
    execute_process(COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(HOMEBREW_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        set(ENV{PKG_CONFIG_PATH} "${HOMEBREW_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    endif()
endif()

# OpenSSL (needed by libwebsockets headers)
if(APPLE AND HOMEBREW_PREFIX)
    set(OPENSSL_ROOT_DIR "${HOMEBREW_PREFIX}/opt/openssl@3")
endif()
find_package(OpenSSL REQUIRED)

# libwebsockets and cjson via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED libwebsockets)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(linkctl-daemon
    main.c
    camera.c
    settings.c
    server.c
    control.c
    mdns.c
)

target_include_directories(linkctl-daemon PRIVATE
    ${LWS_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_directories(linkctl-daemon PRIVATE
    ${LWS_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

target_link_libraries(linkctl-daemon
    ${LWS_LIBRARIES}
    ${CJSON_LIBRARIES}
)

# macOS: IOKit + CoreFoundation for USB access, dns_sd for mDNS
if(APPLE)
    find_library(IOKIT_LIB IOKit REQUIRED)
    find_library(CF_LIB CoreFoundation REQUIRED)
    find_library(DNSSD_LIB dns_sd)
    target_link_libraries(linkctl-daemon ${IOKIT_LIB} ${CF_LIB})
    if(DNSSD_LIB)
        target_link_libraries(linkctl-daemon ${DNSSD_LIB})
    endif()
endif()

install(TARGETS linkctl-daemon DESTINATION bin)

include(CTest)
if(BUILD_TESTING)
    add_executable(test-settings test_settings.c test_settings_fixture.c settings.c)
    target_include_directories(test-settings PRIVATE ${CJSON_INCLUDE_DIRS})
    target_link_directories(test-settings PRIVATE ${CJSON_LIBRARY_DIRS})
    target_link_libraries(test-settings ${CJSON_LIBRARIES})
    target_compile_options(test-settings PRIVATE -UNDEBUG -Wall -Wextra)
    add_test(NAME settings COMMAND test-settings)

    add_executable(test-settings-server test_settings_server.c test_settings_fixture.c settings.c server.c)
    target_include_directories(test-settings-server PRIVATE ${LWS_INCLUDE_DIRS} ${CJSON_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIR})
    target_link_directories(test-settings-server PRIVATE ${LWS_LIBRARY_DIRS} ${CJSON_LIBRARY_DIRS})
    target_link_libraries(test-settings-server ${LWS_LIBRARIES} ${CJSON_LIBRARIES})
    target_compile_options(test-settings-server PRIVATE -UNDEBUG -Wall -Wextra)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import websocket" RESULT_VARIABLE HAS_WEBSOCKET ERROR_QUIET)
        if(HAS_WEBSOCKET EQUAL 0)
            add_test(NAME settings-websocket COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_settings_ws.py $<TARGET_FILE:test-settings-server>)
        else()
            message(STATUS "websocket-client unavailable; settings WebSocket tests can be run manually")
        endif()
    endif()
endif()

# --- Service management targets (macOS launchd) ---

# Install plist to ~/Library/LaunchAgents and load the service
add_custom_target(service-install
    COMMAND ${CMAKE_SOURCE_DIR}/service.sh install ${CMAKE_BINARY_DIR}/linkctl-daemon
    COMMENT "Installing and loading launchd service"
)

# Stop and remove the launchd service
add_custom_target(service-uninstall
    COMMAND ${CMAKE_SOURCE_DIR}/service.sh uninstall
    COMMENT "Removing launchd service"
)

# Rebuild and restart the service
add_custom_target(restart
    COMMAND ${CMAKE_MAKE_PROGRAM} linkctl-daemon
    COMMAND ${CMAKE_SOURCE_DIR}/service.sh restart
    COMMENT "Rebuilding and restarting daemon"
)
