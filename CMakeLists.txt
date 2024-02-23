cmake_minimum_required (VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("asio2exec")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-fconcepts-diagnostics-depth=4 -pthread -Wnon-template-friend ${CMAKE_CXX_FLAGS}")

# 获取examples目录下的所有cpp文件
file(GLOB EXAMPLE_SOURCES "examples/*.cpp")

# 遍历每个cpp文件生成对应的可执行文件
foreach(EXAMPLE_SOURCE ${EXAMPLE_SOURCES})
    # 获取文件名（不包含路径和扩展名）
    get_filename_component(EXAMPLE_NAME ${EXAMPLE_SOURCE} NAME_WE)
    # 生成可执行文件
    add_executable(${EXAMPLE_NAME} ${EXAMPLE_SOURCE})
    target_include_directories(${EXAMPLE_NAME} PUBLIC "asio/asio/include")
    target_include_directories(${EXAMPLE_NAME} PUBLIC "stdexec/include")
    target_include_directories(${EXAMPLE_NAME} PUBLIC ".")
endforeach()

# add_subdirectory("stdexec-main")
# target_link_libraries(${PROJECT_NAME} PRIVATE STDEXEC::stdexec)





