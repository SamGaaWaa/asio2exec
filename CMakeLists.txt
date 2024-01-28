# CMakeList.txt: asio2exec 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("asio2exec")

set(CMAKE_CXX_STANDARD 23)

add_executable(${PROJECT_NAME} main.cpp)

# add_subdirectory("stdexec-main")
# target_link_libraries(${PROJECT_NAME} PRIVATE STDEXEC::stdexec)

set(CMAKE_CXX_FLAGS "-fconcepts-diagnostics-depth=4 -pthread -Wnon-template-friend ${CMAKE_CXX_FLAGS}")


target_include_directories(${PROJECT_NAME} PUBLIC "asio/asio/include")
target_include_directories(${PROJECT_NAME} PUBLIC "stdexec-main/include")

