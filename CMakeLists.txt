cmake_minimum_required(VERSION 3.19)

project(madt DESCRIPTION "MADT server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(MADT_SOFTWARE_VERSION "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --exact-match
        OUTPUT_VARIABLE MADT_GIT_EXACT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --abbrev-ref HEAD
        OUTPUT_VARIABLE MADT_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --always --dirty
        OUTPUT_VARIABLE MADT_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MADT_GIT_EXACT_TAG)
        set(MADT_SOFTWARE_VERSION "${MADT_GIT_EXACT_TAG}")
    elseif(MADT_GIT_BRANCH AND NOT MADT_GIT_BRANCH STREQUAL "HEAD" AND MADT_GIT_DESCRIBE)
        set(MADT_SOFTWARE_VERSION "${MADT_GIT_BRANCH} (${MADT_GIT_DESCRIBE})")
    elseif(MADT_GIT_BRANCH AND NOT MADT_GIT_BRANCH STREQUAL "HEAD")
        set(MADT_SOFTWARE_VERSION "${MADT_GIT_BRANCH}")
    elseif(MADT_GIT_DESCRIBE)
        set(MADT_SOFTWARE_VERSION "${MADT_GIT_DESCRIBE}")
    endif()
endif()

option(MADT_USE_WEBENGINEVIEW "Use Qt WebEngine instead of Qt WebKit" ON)
set(MADT_LOG_LEVEL "INFO" CACHE STRING "Compile-time log level: DEBUG, INFO, WARN, or ERROR")
set_property(CACHE MADT_LOG_LEVEL PROPERTY STRINGS DEBUG INFO WARN ERROR)
set(MADT_LIBEVENT_ROOT "" CACHE PATH "Prefix containing a libevent installation")

string(TOUPPER "${MADT_LOG_LEVEL}" MADT_LOG_LEVEL_NORMALIZED)
set(MADT_LOG_LEVEL_VALUE)
if(MADT_LOG_LEVEL_NORMALIZED STREQUAL "DEBUG")
    set(MADT_LOG_LEVEL_VALUE 0)
elseif(MADT_LOG_LEVEL_NORMALIZED STREQUAL "INFO")
    set(MADT_LOG_LEVEL_VALUE 1)
elseif(MADT_LOG_LEVEL_NORMALIZED STREQUAL "WARN")
    set(MADT_LOG_LEVEL_VALUE 2)
elseif(MADT_LOG_LEVEL_NORMALIZED STREQUAL "ERROR")
    set(MADT_LOG_LEVEL_VALUE 3)
else()
    message(FATAL_ERROR "Invalid MADT_LOG_LEVEL='${MADT_LOG_LEVEL}'. Use DEBUG, INFO, WARN, or ERROR.")
endif()

find_package(nlohmann_json REQUIRED)

set(_madt_libevent_include_hints)
set(_madt_libevent_library_hints)
if(MADT_LIBEVENT_ROOT)
    list(APPEND _madt_libevent_include_hints "${MADT_LIBEVENT_ROOT}/include")
    list(APPEND _madt_libevent_library_hints "${MADT_LIBEVENT_ROOT}/lib" "${MADT_LIBEVENT_ROOT}/lib64")
endif()

find_path(EVENT2_INCLUDE_DIR
    NAMES event2/event.h
    HINTS ${_madt_libevent_include_hints}
)
find_library(EVENT2_LIBRARY
    NAMES event
    HINTS ${_madt_libevent_library_hints}
)
find_library(EVENT2_PTHREADS_LIBRARY
    NAMES event_pthreads
    HINTS ${_madt_libevent_library_hints}
)

if(NOT EVENT2_INCLUDE_DIR OR NOT EVENT2_LIBRARY OR NOT EVENT2_PTHREADS_LIBRARY)
    message(FATAL_ERROR
        "libevent was not found. Build libevent-2.2.1-alpha-dev and configure with "
        "-DMADT_LIBEVENT_ROOT=<libevent-install-prefix>."
    )
endif()

add_library(Libevent::Libevent UNKNOWN IMPORTED)
set_target_properties(Libevent::Libevent PROPERTIES
    IMPORTED_LOCATION "${EVENT2_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT2_INCLUDE_DIR}"
)

add_library(Libevent::LibeventPthreads UNKNOWN IMPORTED)
set_target_properties(Libevent::LibeventPthreads PROPERTIES
    IMPORTED_LOCATION "${EVENT2_PTHREADS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${EVENT2_INCLUDE_DIR}"
)

add_library(wrapper_log SHARED
    loghelper/cwrapper.cpp
)
target_include_directories(wrapper_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(wrapper_log PUBLIC MADT_LOG_LEVEL=${MADT_LOG_LEVEL_VALUE})
set_target_properties(wrapper_log PROPERTIES
    AUTOMOC OFF
    OUTPUT_NAME "wrapper-log"
)

add_library(wrapper_process SHARED
    process/process.cpp
)
target_include_directories(wrapper_process PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(wrapper_process PUBLIC MADT_LOG_LEVEL=${MADT_LOG_LEVEL_VALUE})
target_link_libraries(wrapper_process PUBLIC
    wrapper_log
    Libevent::Libevent
    Libevent::LibeventPthreads
)
set_target_properties(wrapper_process PROPERTIES
    AUTOMOC OFF
    OUTPUT_NAME "wrapper-process"
)

add_library(wrapper_wdog SHARED
    utils/wdog.cpp
)
target_include_directories(wrapper_wdog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(wrapper_wdog PROPERTIES
    AUTOMOC OFF
    OUTPUT_NAME "wrapper-wdog"
)

add_subdirectory("madt-server")

install(TARGETS wrapper_log wrapper_process wrapper_wdog
    LIBRARY DESTINATION lib
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
    DESTINATION "share/doc/madt"
)
