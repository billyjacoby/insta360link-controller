cmake_minimum_required(VERSION 3.14)
project(linkctl-daemon C)

set(CMAKE_C_STANDARD 11)

# Prefer Homebrew paths on macOS
if(APPLE)
    list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()

# libuvc has cmake config files
find_package(libuvc REQUIRED)

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

target_link_libraries(linkctl-daemon LibUVC::UVCShared)

target_include_directories(linkctl-daemon PRIVATE
    ${LWS_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_directories(linkctl-daemon PRIVATE
    ${LWS_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

target_link_libraries(linkctl-daemon
    ${LWS_LIBRARIES}
    ${CJSON_LIBRARIES}
)

# macOS: link dns_sd (built-in)
if(APPLE)
    find_library(DNSSD_LIB dns_sd)
    if(DNSSD_LIB)
        target_link_libraries(linkctl-daemon ${DNSSD_LIB})
    endif()
endif()

install(TARGETS linkctl-daemon DESTINATION bin)
