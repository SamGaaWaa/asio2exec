cmake_minimum_required (VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("asio2exec")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_FLAGS "-fconcepts-diagnostics-depth=4 -pthread -Wnon-template-friend ${CMAKE_CXX_FLAGS}")

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/stdexec")
    message(STATUS "Cloning stdexec.")
    execute_process(
        COMMAND git clone https://github.com/NVIDIA/stdexec.git
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE git_clone_result
        OUTPUT_VARIABLE git_output
        ERROR_VARIABLE git_error
        )

    if(git_clone_result GREATER 0)
        message(FATAL_ERROR "Failed to clone repository: ${git_output}")
    endif()
else()
    message(STATUS "Found stdexec.")
endif()

file(GLOB EXAMPLE_SOURCES "examples/*.cpp")

foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_include_directories(${EXAMPLE_NAME} PUBLIC "asio/asio/include")
    target_include_directories(${EXAMPLE_NAME} PUBLIC "stdexec/include")
    target_include_directories(${EXAMPLE_NAME} PUBLIC ".")
endforeach()






