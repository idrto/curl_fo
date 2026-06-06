if(DEFINED ENV{VCPKG_ROOT})
    set(_vcpkg_root "$ENV{VCPKG_ROOT}")
else()
    get_filename_component(_vcpkg_root "${CMAKE_CURRENT_LIST_DIR}/../vcpkg" ABSOLUTE)
endif()

if(NOT EXISTS "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR "vcpkg not found at ${_vcpkg_root}")
endif()

include("${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
