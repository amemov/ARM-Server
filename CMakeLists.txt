cmake_minimum_required(VERSION 3.12)
project(SerialServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(GTest REQUIRED)

# Main server executable
add_executable(server
    server.cpp
    serial_interface.cpp
    server_api.cpp
)

target_include_directories(server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(server PRIVATE 
    sqlite3
    CURL::libcurl
)

# Test executable
add_executable(tests
    server_integration_test.cpp
)

target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tests PRIVATE
    GTest::GTest
    GTest::Main
)

enable_testing()
add_test(NAME AllTests COMMAND tests)