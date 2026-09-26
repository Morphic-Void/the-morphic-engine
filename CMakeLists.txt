# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)

cmake_minimum_required(VERSION 3.22)
project(MorphicEngine LANGUAGES CXX)

# The native Visual Studio solution remains the Windows build entry point.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This experimental CMake build currently targets Linux only.")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_CONFIGURATION_TYPES)
    message(FATAL_ERROR "The initial Linux canary supports single-configuration Debug builds only.")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "The initial Linux canary supports GCC and Clang.")
endif()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(morphic_sources output project_file)
    set(manifest "${CMAKE_BINARY_DIR}/sources/${output}.cmake")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/cmake_sources.py"
            "${CMAKE_SOURCE_DIR}/${project_file}" --root "${CMAKE_SOURCE_DIR}"
            --output "${manifest}"
        COMMAND_ERROR_IS_FATAL ANY
    )
    include("${manifest}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${VS_PROJECTS} "${CMAKE_SOURCE_DIR}/tools/cmake_sources.py")
    set(${output} ${VS_SOURCES} PARENT_SCOPE)
endfunction()

morphic_sources(suite_sources external/SuiteUTF/SuiteUTF.vcxproj)
add_library(SuiteUTF STATIC ${suite_sources})
target_include_directories(SuiteUTF PUBLIC "${CMAKE_SOURCE_DIR}/external/SuiteUTF/include")
target_compile_options(SuiteUTF PRIVATE -fno-exceptions)
target_compile_definitions(SuiteUTF PRIVATE _DEBUG)
set_target_properties(SuiteUTF PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)

morphic_sources(policy_sources visual_studio/MorphicPolicyValidator.vcxproj)
add_executable(MorphicPolicyValidator ${policy_sources})
target_link_libraries(MorphicPolicyValidator PRIVATE Threads::Threads)
add_custom_target(MorphicPolicyCheck
    COMMAND "$<TARGET_FILE:MorphicPolicyValidator>" --root "${CMAKE_SOURCE_DIR}"
        --project LinuxCanary --configuration Debug --platform x64
    DEPENDS MorphicPolicyValidator
    VERBATIM
)

foreach(target IN ITEMS MorphicHost MorphicExecutive MorphicRendering MorphicTests)
    morphic_sources(target_sources "visual_studio/${target}.vcxproj")
    if(target STREQUAL "MorphicExecutive" OR target STREQUAL "MorphicRendering")
        add_library(${target} SHARED ${target_sources})
        # Existing module paths use .dll, including the ErasedOwner test.
        # These files contain native Linux ELF shared libraries, loaded by dlopen.
        set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".dll")
    else()
        add_executable(${target} ${target_sources})
    endif()
    target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/core")
    target_compile_definitions(${target} PRIVATE
        _DEBUG MV_DEVELOPMENT_BUILD=1 MV_DEBUG_DEVELOPMENT_BUILD=1 MV_COMPILED_INFORMATION_LEVEL=3)
    target_compile_options(${target} PRIVATE -fno-exceptions)
    target_link_libraries(${target} PRIVATE SuiteUTF Threads::Threads ${CMAKE_DL_LIBS})
    # Core is compiled separately into each executable/module, as in Visual Studio.
    # Hidden symbols preserve module-local runtime state on ELF platforms.
    set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
    add_dependencies(${target} MorphicPolicyCheck)
endforeach()
set_target_properties(MorphicHost PROPERTIES OUTPUT_NAME MorphicEngine)
add_dependencies(MorphicHost MorphicExecutive MorphicRendering)
add_dependencies(MorphicTests MorphicExecutive MorphicRendering)

enable_testing()
add_test(NAME MorphicOrdinary COMMAND MorphicTests -t1 --log-tag=linux-canary
    "--output-directory=${CMAKE_BINARY_DIR}/test-output")
set_tests_properties(MorphicOrdinary PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 600)
