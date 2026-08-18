cmake_minimum_required(VERSION 3.15)

project(chat-client LANGUAGES CXX)

find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(chat-client
    src/main.cpp
    src/command.cpp
    src/connection.cpp
    src/message.cpp
    src/protocol.cpp
)

target_include_directories(chat-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(chat-client PRIVATE ws2_32 OpenSSL::SSL OpenSSL::Crypto)

if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(chat-client PRIVATE -municode)
endif()

add_executable(command-tests
    tests/command_tests.cpp
    src/command.cpp
)

target_include_directories(command-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(protocol-tests
    tests/protocol_tests.cpp
    src/message.cpp
    src/protocol.cpp
)

target_include_directories(protocol-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

find_package(Threads REQUIRED)
target_link_libraries(protocol-tests PRIVATE ws2_32 Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

add_executable(connection-tests
    tests/connection_tests.cpp
    src/connection.cpp
    src/message.cpp
    src/protocol.cpp
)

target_include_directories(connection-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(connection-tests PRIVATE ws2_32 Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

enable_testing()
add_test(NAME command-tests COMMAND command-tests)
add_test(NAME protocol-tests COMMAND protocol-tests)
add_test(NAME connection-tests COMMAND connection-tests)
