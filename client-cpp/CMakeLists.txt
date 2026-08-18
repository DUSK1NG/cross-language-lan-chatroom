cmake_minimum_required(VERSION 3.15)

project(chat-client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chat-client
    src/main.cpp
    src/message.cpp
    src/protocol.cpp
)

target_include_directories(chat-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(chat-client PRIVATE ws2_32)

add_executable(protocol-tests
    tests/protocol_tests.cpp
    src/message.cpp
    src/protocol.cpp
)

target_include_directories(protocol-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(protocol-tests PRIVATE ws2_32)

enable_testing()
add_test(NAME protocol-tests COMMAND protocol-tests)
