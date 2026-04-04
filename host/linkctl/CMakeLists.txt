cmake_minimum_required(VERSION 3.14)
project(linkctl C)

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

add_executable(linkctl linkctl.c)

target_include_directories(linkctl PRIVATE
    ${LWS_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_directories(linkctl PRIVATE
    ${LWS_LIBRARY_DIRS}
    ${CJSON_LIBRARY_DIRS}
)

target_link_libraries(linkctl
    ${LWS_LIBRARIES}
    ${CJSON_LIBRARIES}
)

install(TARGETS linkctl DESTINATION bin)
