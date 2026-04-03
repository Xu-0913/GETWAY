cmake_minimum_required(VERSION 3.10)

project(GETWAY LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)

add_compile_options(-w)

find_package(nlohmann_json REQUIRED)

add_executable(GETWAY
    Buffer.cpp
    Device.cpp
    Message.cpp
    Route.cpp
    Serial.cpp
    ThreadPool.cpp
    _MQTTClient.cpp
    _MQTTCallback.cpp
    bluetooth.cpp
    run.cpp
    test.cpp
    ../Public/_public.cpp
)

target_include_directories(GETWAY PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    /home/xu/GetWay/Public
)

target_link_libraries(GETWAY PRIVATE
    pthread
    paho-mqttpp3
    paho-mqtt3as
)

add_custom_command(TARGET GETWAY POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_BINARY_DIR}/GETWAY
    /home/xu/GetWay/bin
)