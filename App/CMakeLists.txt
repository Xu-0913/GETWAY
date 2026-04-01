cmake_minimum_required(VERSION 3.10)

project(GETWAY LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)  
add_compile_options(-w)


add_executable(Buffer
    Buffer.cpp
    ../Public/_public.cpp
)

add_executable(MQTTClient
    _MQTTClient.cpp
    _MQTTCallback.cpp
    ../Public/_public.cpp
)

add_executable(Message
    Message.cpp
    ../Public/_public.cpp
)

add_executable(ThreadPool
    ThreadPool.cpp
    ../Public/_public.cpp
)

target_link_libraries(Buffer PRIVATE  pthread ) #

target_link_libraries(MQTTClient PRIVATE paho-mqttpp3 paho-mqtt3as pthread)

target_link_libraries(ThreadPool PRIVATE pthread)


set(CMAKE_BUILD_TYPE Debug)  #生成调试信息

add_custom_command(TARGET Buffer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/Buffer
    /home/xu/GetWay/bin
)

add_custom_command(TARGET MQTTClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/MQTTClient
    /home/xu/GetWay/bin
)

add_custom_command(TARGET Message POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/Message
    /home/xu/GetWay/bin
)

add_custom_command(TARGET ThreadPool POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/ThreadPool
    /home/xu/GetWay/bin
)
























