cmake_minimum_required(VERSION 3.22)
project(godot_sip LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(GODOT_SIP_BUILD_SHARED "Build shared library for GDExtension" ON)

if (NOT DEFINED GODOT_CPP_PATH)
  message(FATAL_ERROR "GODOT_CPP_PATH is not set. Point it to a built godot-cpp checkout.")
endif()
if (NOT EXISTS "${GODOT_CPP_PATH}/CMakeLists.txt")
  message(FATAL_ERROR "GODOT_CPP_PATH does not look like a godot-cpp checkout: ${GODOT_CPP_PATH}")
endif()

set(PJSIP_PATH "" CACHE PATH "Path to a built PJSIP tree (root containing pjlib/include).")
if (PJSIP_PATH STREQUAL "")
  message(WARNING "PJSIP_PATH is not set. Build will succeed but SIP functionality will be disabled.")
endif()

set(GODOT_SIP_ENABLE_PJSIP OFF)
if (NOT PJSIP_PATH STREQUAL "")
  if (EXISTS "${PJSIP_PATH}/pjsip/include/pjsua-lib/pjsua.h")
    set(GODOT_SIP_ENABLE_PJSIP ON)
  else()
    message(WARNING "PJSIP_PATH is set but pjsua header was not found at ${PJSIP_PATH}/pjsip/include/pjsua-lib/pjsua.h. SIP will be disabled.")
  endif()
endif()

add_subdirectory(${GODOT_CPP_PATH} godot-cpp)

add_library(godot_sip SHARED
  src/register_types.cpp
  src/sip_client.cpp
  src/sip_call.cpp
  src/pjsip_wrapper.cpp
)

set(GODOT_SIP_PLATFORM "" CACHE STRING "godot platform name (windows/linux/macos)")
set(GODOT_SIP_ARCH "" CACHE STRING "godot arch name (x86_64/arm64)")

if (GODOT_SIP_PLATFORM STREQUAL "")
  if (WIN32)
    set(GODOT_SIP_PLATFORM "windows")
  elseif(APPLE)
    set(GODOT_SIP_PLATFORM "macos")
  else()
    set(GODOT_SIP_PLATFORM "linux")
  endif()
endif()

if (GODOT_SIP_ARCH STREQUAL "")
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(GODOT_SIP_ARCH "arm64")
  else()
    set(GODOT_SIP_ARCH "x86_64")
  endif()
endif()

set(GODOT_SIP_CONFIG "$<IF:$<CONFIG:Debug>,debug,release>")
set_target_properties(godot_sip PROPERTIES
  PREFIX ""
  OUTPUT_NAME "godot_sip.${GODOT_SIP_PLATFORM}.${GODOT_SIP_CONFIG}.${GODOT_SIP_ARCH}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(godot_sip PRIVATE include)

target_link_libraries(godot_sip PRIVATE godot-cpp)

target_compile_definitions(godot_sip PRIVATE $<$<BOOL:${GODOT_SIP_ENABLE_PJSIP}>:GODOT_SIP_HAS_PJSIP>)

if (GODOT_SIP_ENABLE_PJSIP)
  set(PJ_CONFIG_SITE_INCLUDE_ROOT "")
  if (EXISTS "${PJSIP_PATH}/pjlib/include/pj/config_site.h")
    set(PJ_CONFIG_SITE_INCLUDE_ROOT "${PJSIP_PATH}/pjlib/include")
  else()
    set(PJ_CONFIG_SITE_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/pj")
    file(MAKE_DIRECTORY "${PJ_CONFIG_SITE_GENERATED_DIR}")
    if (EXISTS "${PJSIP_PATH}/pjlib/include/pj/config_site_sample.h")
      configure_file(
        "${PJSIP_PATH}/pjlib/include/pj/config_site_sample.h"
        "${PJ_CONFIG_SITE_GENERATED_DIR}/config_site.h"
        COPYONLY
      )
    else()
      file(WRITE "${PJ_CONFIG_SITE_GENERATED_DIR}/config_site.h" "#pragma once\n")
      message(WARNING "PJSIP config_site_sample.h was not found. Generated minimal config_site.h; you may need to customize it for your environment.")
    endif()
    set(PJ_CONFIG_SITE_INCLUDE_ROOT "${CMAKE_BINARY_DIR}/generated")
    message(STATUS "Using generated pj/config_site.h from ${PJ_CONFIG_SITE_INCLUDE_ROOT}")
  endif()

  target_include_directories(godot_sip PRIVATE
    ${PJ_CONFIG_SITE_INCLUDE_ROOT}
    ${PJSIP_PATH}/pjlib/include
    ${PJSIP_PATH}/pjlib-util/include
    ${PJSIP_PATH}/pjnath/include
    ${PJSIP_PATH}/pjsip/include
    ${PJSIP_PATH}/pjmedia/include
    ${PJSIP_PATH}/third_party/srtp/include
    ${PJSIP_PATH}/third_party/opus/include
  )

  if (WIN32)
    file(GLOB_RECURSE PJSIP_ALL_LIBS "${PJSIP_PATH}/*.lib")
    if (PJSIP_ALL_LIBS STREQUAL "")
      message(FATAL_ERROR "No PJSIP .lib files found under ${PJSIP_PATH}. Build pjproject first.")
    endif()

    function(find_pjsip_lib stem out_var)
      set(found_lib "")
      foreach(lib_path IN LISTS PJSIP_ALL_LIBS)
        if (lib_path MATCHES "([/\\\\])${stem}[^/\\\\]*\\.lib$")
          set(found_lib "${lib_path}")
          break()
        endif()
      endforeach()
      if (found_lib STREQUAL "")
        message(FATAL_ERROR "Could not find library matching '${stem}*.lib' under ${PJSIP_PATH}.")
      endif()
      set(${out_var} "${found_lib}" PARENT_SCOPE)
    endfunction()

    find_pjsip_lib("pjsua-lib-${GODOT_SIP_ARCH}" PJSIP_LIB_PJSUA)
    find_pjsip_lib("pjsip-core-${GODOT_SIP_ARCH}" PJSIP_LIB_PJSIP)
    find_pjsip_lib("pjsip-simple-${GODOT_SIP_ARCH}" PJSIP_LIB_PJSIP_SIMPLE)
    find_pjsip_lib("pjsip-ua-${GODOT_SIP_ARCH}" PJSIP_LIB_PJSIP_UA)
    find_pjsip_lib("pjnath-${GODOT_SIP_ARCH}" PJSIP_LIB_PJNATH)
    find_pjsip_lib("pjmedia-${GODOT_SIP_ARCH}" PJSIP_LIB_PJMEDIA)
    find_pjsip_lib("pjmedia-codec-${GODOT_SIP_ARCH}" PJSIP_LIB_PJMEDIA_CODEC)
    find_pjsip_lib("pjmedia-audiodev-${GODOT_SIP_ARCH}" PJSIP_LIB_PJMEDIA_AUDIODEV)
    find_pjsip_lib("pjmedia-videodev-${GODOT_SIP_ARCH}" PJSIP_LIB_PJMEDIA_VIDEODEV)
    find_pjsip_lib("libsrtp-${GODOT_SIP_ARCH}" PJSIP_LIB_SRTP)
    find_pjsip_lib("libspeex-${GODOT_SIP_ARCH}" PJSIP_LIB_SPEEX)
    find_pjsip_lib("libgsmcodec-${GODOT_SIP_ARCH}" PJSIP_LIB_GSM)
    find_pjsip_lib("libilbccodec-${GODOT_SIP_ARCH}" PJSIP_LIB_ILBC)
    find_pjsip_lib("libresample-${GODOT_SIP_ARCH}" PJSIP_LIB_RESAMPLE)
    find_pjsip_lib("pjlib-util-${GODOT_SIP_ARCH}" PJSIP_LIB_PJLIB_UTIL)
    find_pjsip_lib("pjlib-${GODOT_SIP_ARCH}" PJSIP_LIB_PJLIB)

    target_link_libraries(godot_sip PRIVATE
      ${PJSIP_LIB_PJSUA}
      ${PJSIP_LIB_PJSIP}
      ${PJSIP_LIB_PJSIP_SIMPLE}
      ${PJSIP_LIB_PJSIP_UA}
      ${PJSIP_LIB_PJNATH}
      ${PJSIP_LIB_PJMEDIA}
      ${PJSIP_LIB_PJMEDIA_CODEC}
      ${PJSIP_LIB_PJMEDIA_AUDIODEV}
      ${PJSIP_LIB_PJMEDIA_VIDEODEV}
      ${PJSIP_LIB_SRTP}
      ${PJSIP_LIB_SPEEX}
      ${PJSIP_LIB_GSM}
      ${PJSIP_LIB_ILBC}
      ${PJSIP_LIB_RESAMPLE}
      ${PJSIP_LIB_PJLIB_UTIL}
      ${PJSIP_LIB_PJLIB}
    )
  elseif(ANDROID)
    file(GLOB_RECURSE PJSIP_ALL_LIBS_A "${PJSIP_PATH}/*.a")
    if (PJSIP_ALL_LIBS_A STREQUAL "")
      message(FATAL_ERROR "No PJSIP .a files found under ${PJSIP_PATH}. Build pjproject first.")
    endif()

    function(find_pjsip_lib_a stem out_var)
      set(found_lib "")
      foreach(lib_path IN LISTS PJSIP_ALL_LIBS_A)
        if (lib_path MATCHES "([/\\\\])lib${stem}[^/\\\\]*\\.a$")
          set(found_lib "${lib_path}")
          break()
        endif()
      endforeach()
      if (found_lib STREQUAL "")
        message(FATAL_ERROR "Could not find library matching 'lib${stem}*.a' under ${PJSIP_PATH}.")
      endif()
      set(${out_var} "${found_lib}" PARENT_SCOPE)
    endfunction()

    find_pjsip_lib_a("pjsua" PJSIP_LIB_PJSUA)
    find_pjsip_lib_a("pjsip" PJSIP_LIB_PJSIP)
    find_pjsip_lib_a("pjsip-simple" PJSIP_LIB_PJSIP_SIMPLE)
    find_pjsip_lib_a("pjsip-ua" PJSIP_LIB_PJSIP_UA)
    find_pjsip_lib_a("pjnath" PJSIP_LIB_PJNATH)
    find_pjsip_lib_a("pjmedia" PJSIP_LIB_PJMEDIA)
    find_pjsip_lib_a("pjmedia-codec" PJSIP_LIB_PJMEDIA_CODEC)
    find_pjsip_lib_a("pjmedia-audiodev" PJSIP_LIB_PJMEDIA_AUDIODEV)
    find_pjsip_lib_a("pjmedia-videodev" PJSIP_LIB_PJMEDIA_VIDEODEV)
    find_pjsip_lib_a("srtp" PJSIP_LIB_SRTP)
    find_pjsip_lib_a("speex" PJSIP_LIB_SPEEX)
    find_pjsip_lib_a("gsmcodec" PJSIP_LIB_GSM)
    find_pjsip_lib_a("ilbccodec" PJSIP_LIB_ILBC)
    find_pjsip_lib_a("resample" PJSIP_LIB_RESAMPLE)
    find_pjsip_lib_a("pjlib-util" PJSIP_LIB_PJLIB_UTIL)
    find_pjsip_lib_a("pjlib" PJSIP_LIB_PJLIB)

    target_link_libraries(godot_sip PRIVATE
      ${PJSIP_LIB_PJSUA}
      ${PJSIP_LIB_PJSIP}
      ${PJSIP_LIB_PJSIP_SIMPLE}
      ${PJSIP_LIB_PJSIP_UA}
      ${PJSIP_LIB_PJNATH}
      ${PJSIP_LIB_PJMEDIA}
      ${PJSIP_LIB_PJMEDIA_CODEC}
      ${PJSIP_LIB_PJMEDIA_AUDIODEV}
      ${PJSIP_LIB_PJMEDIA_VIDEODEV}
      ${PJSIP_LIB_SRTP}
      ${PJSIP_LIB_SPEEX}
      ${PJSIP_LIB_GSM}
      ${PJSIP_LIB_ILBC}
      ${PJSIP_LIB_RESAMPLE}
      ${PJSIP_LIB_PJLIB_UTIL}
      ${PJSIP_LIB_PJLIB}
      log
      OpenSLES
      android
      m
      dl
    )
  else()
    target_link_directories(godot_sip PRIVATE
      ${PJSIP_PATH}/lib
    )

    target_link_libraries(godot_sip PRIVATE
      pjsua
      pjsip
      pjsip-simple
      pjsip-ua
      pjnath
      pjlib-util
      pjlib
    )
  endif()

  if (WIN32)
    target_link_libraries(godot_sip PRIVATE ws2_32 winmm iphlpapi secur32 ole32 bcrypt)
  elseif(APPLE)
    find_library(COREAUDIO_LIB CoreAudio)
    find_library(AUDIOTOOLBOX_LIB AudioToolbox)
    find_library(AVFOUNDATION_LIB AVFoundation)
    find_library(COREFOUNDATION_LIB CoreFoundation)
    target_link_libraries(godot_sip PRIVATE ${COREAUDIO_LIB} ${AUDIOTOOLBOX_LIB} ${AVFOUNDATION_LIB} ${COREFOUNDATION_LIB})
  else()
    target_link_libraries(godot_sip PRIVATE pthread dl m)
  endif()
endif()

if (WIN32)
  target_compile_definitions(godot_sip PRIVATE NOMINMAX)
endif()

install(TARGETS godot_sip DESTINATION bin)
