cmake_minimum_required(VERSION 3.10)

project(GETWAY LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-w)


add_executable(Buffer
    Buffer.cpp
    ../Public/_public.cpp
)

target_link_libraries(Buffer PRIVATE  pthread ) #
set(CMAKE_BUILD_TYPE Debug)  #生成调试信息

add_custom_command(TARGET Buffer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/Buffer
    /home/xu/GetWay/App/build
)




























