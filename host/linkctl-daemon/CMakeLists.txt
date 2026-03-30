cmake_minimum_required(VERSION 3.14)
project(linkctl-daemon C)

set(CMAKE_C_STANDARD 11)

# Prefer Homebrew paths on macOS
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

# OpenSSL (needed by libwebsockets headers)
if(APPLE)
    set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
endif()
find_package(OpenSSL REQUIRED)

# libwebsockets and cjson via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED libwebsockets)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(linkctl-daemon
    main.c
    camera.c
    server.c
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
