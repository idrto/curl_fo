cmake_minimum_required(VERSION 3.16)
project(curl_fo VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CURL_FO_MOBILE_BUILD "Android/iOS build (no shim/wrapper/CLI)" OFF)
option(CURL_FO_BUILD_SHIM "Build LD_PRELOAD shim" ON)
option(CURL_FO_BUILD_WRAPPER "Build libcurl-compatible wrapper" ON)
option(CURL_FO_BUILD_CLI "Build curl-fo CLI" ON)
option(CURL_FO_BUILD_TESTS "Build tests" ON)
option(CURL_FO_BUILD_EXAMPLE "Build example binary" OFF)

if(CURL_FO_MOBILE_BUILD)
    set(CURL_FO_BUILD_SHIM OFF CACHE BOOL "" FORCE)
    set(CURL_FO_BUILD_WRAPPER OFF CACHE BOOL "" FORCE)
    set(CURL_FO_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(CURL_FO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CURL_FO_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
endif()

# Allow -DCURL_ROOT= or -DCURL_INCLUDE_DIR= / -DCURL_LIBRARY=
if(DEFINED ENV{VCPKG_ROOT})
    if(DEFINED VCPKG_TARGET_TRIPLET)
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
    else()
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
    endif()
endif()
find_package(CURL QUIET)
if(NOT CURL_FOUND)
    find_path(CURL_INCLUDE_DIR NAMES curl/curl.h
        HINTS "${CURL_ROOT}/include" "C:/curl/include" "$ENV{CURL_ROOT}/include"
              "${CMAKE_SOURCE_DIR}/deps/install/include")
    find_library(CURL_LIBRARY NAMES curl libcurl libcurl_imp libcurl.a
        HINTS "${CURL_ROOT}/lib" "C:/curl/lib" "$ENV{CURL_ROOT}/lib"
              "${CMAKE_SOURCE_DIR}/deps/install/lib")
    if(CURL_INCLUDE_DIR AND CURL_LIBRARY)
        add_library(CURL::libcurl UNKNOWN IMPORTED)
        set_target_properties(CURL::libcurl PROPERTIES
            IMPORTED_LOCATION "${CURL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIR}")
        set(CURL_FOUND TRUE)
    endif()
endif()
if(NOT CURL_FOUND)
    message(FATAL_ERROR
        "libcurl not found. Install libcurl dev package or pass "
        "-DCURL_ROOT=<path> with include/ and lib/ subdirs.")
endif()

set(CURL_FO_CORE_SOURCES
    src/config.c
    src/util.c
    src/dns.c
    src/cache.c
    src/probe.c
    src/http.c
    src/ws.c
    src/handle.c
    src/curl_dispatch.c
    src/shim_ctx.c
)

add_library(curl_fo SHARED ${CURL_FO_CORE_SOURCES})
target_include_directories(curl_fo
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(curl_fo PRIVATE CURL_FO_BUILDING)
target_link_libraries(curl_fo PRIVATE CURL::libcurl)
set_target_properties(curl_fo PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME curl_fo
    PUBLIC_HEADER include/curl_fo.h)

if(WIN32)
    target_link_libraries(curl_fo PRIVATE ws2_32 iphlpapi)
elseif(ANDROID OR (APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    target_link_libraries(curl_fo PRIVATE log)
endif()

add_library(curl_fo_static STATIC ${CURL_FO_CORE_SOURCES})
target_include_directories(curl_fo_static
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(curl_fo_static PRIVATE CURL_FO_BUILDING)
target_link_libraries(curl_fo_static PRIVATE CURL::libcurl)
if(WIN32)
    target_link_libraries(curl_fo_static PRIVATE ws2_32 iphlpapi)
endif()

if(CURL_FO_BUILD_SHIM)
    add_library(curl_fo_shim SHARED src/shim.c ${CURL_FO_CORE_SOURCES})
    target_include_directories(curl_fo_shim PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(curl_fo_shim PRIVATE CURL_FO_BUILDING CURL_FO_SHIM_BUILD)
    target_link_libraries(curl_fo_shim PRIVATE)
    if(WIN32)
        target_link_libraries(curl_fo_shim PRIVATE ws2_32 iphlpapi)
    endif()
    set_target_properties(curl_fo_shim PROPERTIES OUTPUT_NAME curl_fo_shim)
    if(LINUX)
        set_target_properties(curl_fo_shim PROPERTIES
            LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/src/shim.version")
    endif()
endif()

if(CURL_FO_BUILD_WRAPPER)
    add_library(curl_fo_wrapper SHARED src/wrapper.c ${CURL_FO_CORE_SOURCES})
    target_include_directories(curl_fo_wrapper PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(curl_fo_wrapper PRIVATE CURL_FO_BUILDING CURL_FO_WRAPPER_BUILD)
    target_link_libraries(curl_fo_wrapper PRIVATE)
    if(WIN32)
        target_link_libraries(curl_fo_wrapper PRIVATE ws2_32 iphlpapi)
    endif()
    set_target_properties(curl_fo_wrapper PROPERTIES OUTPUT_NAME curl_fo_wrapper)
    if(LINUX)
        set_target_properties(curl_fo_wrapper PROPERTIES
            LINK_FLAGS "-Wl,--version-script=${CMAKE_SOURCE_DIR}/src/wrapper.version")
    endif()
endif()

if(CURL_FO_BUILD_CLI)
    add_executable(curl-fo cli/curl_fo_main.c)
    target_include_directories(curl-fo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(curl-fo PRIVATE curl_fo CURL::libcurl)
endif()

if(CURL_FO_BUILD_TESTS)
    enable_testing()
    add_executable(curl_fo_tests
        tests/test_main.c
        tests/test_util.c
        tests/test_failover.c
        tests/test_cache.c
        tests/test_probe.c
        tests/test_dns.c
        tests/test_ttl.c
    )
    target_include_directories(curl_fo_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(curl_fo_tests PRIVATE CURL_FO_BUILDING CURL_FO_TESTING)
    target_link_libraries(curl_fo_tests PRIVATE curl_fo_static CURL::libcurl)
    if(WIN32)
        target_link_libraries(curl_fo_tests PRIVATE ws2_32 iphlpapi)
    endif()
    add_test(NAME curl_fo_tests COMMAND curl_fo_tests)
endif()

if(CURL_FO_BUILD_EXAMPLE)
    add_executable(curl_fo_example examples/simple_http.c)
    target_include_directories(curl_fo_example PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(curl_fo_example PRIVATE curl_fo CURL::libcurl)
endif()

# ── Install / packaging ───────────────────────────────────────────────────
install(TARGETS curl_fo curl_fo_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION lib
    PUBLIC_HEADER DESTINATION include)
install(FILES include/curl_fo.h DESTINATION include)

if(CURL_FO_BUILD_SHIM)
    install(TARGETS curl_fo_shim
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION lib)
endif()

if(CURL_FO_BUILD_WRAPPER)
    install(TARGETS curl_fo_wrapper
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION lib)
endif()

if(CURL_FO_BUILD_CLI)
    install(TARGETS curl-fo RUNTIME DESTINATION bin)
endif()
