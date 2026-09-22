cmake_minimum_required(VERSION 3.14)
project(crafty VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

option(CRAFTY_SANITIZE "Build with -fsanitize=address,undefined" OFF)
if(CRAFTY_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS 
  proto/rpc.proto
  proto/kv.proto
)   

include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-30-2
)
FetchContent_MakeAvailable(asio)

add_executable(crafty_node 
    src/node.cpp 
    src/rpc.cpp
    src/kv.cpp
    src/cluster.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(crafty_node PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(crafty_node PRIVATE ASIO_STANDALONE)
target_link_libraries(crafty_node PRIVATE
    Threads::Threads
    protobuf::libprotobuf
)

add_executable(crafty_client
    src/client.cpp 
    src/cluster.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(crafty_client PRIVATE 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(crafty_client PRIVATE ASIO_STANDALONE)
target_link_libraries(crafty_client PRIVATE
    Threads::Threads
    protobuf::libprotobuf
)

enable_testing()

add_executable(crafty_tests
    tests/unit_tests.cpp
    src/kv.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(crafty_tests PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${Protobuf_INCLUDE_DIRS}
    ${asio_SOURCE_DIR}/asio/include
)

target_compile_definitions(crafty_tests PRIVATE ASIO_STANDALONE)
target_link_libraries(crafty_tests PRIVATE
    Threads::Threads
    protobuf::libprotobuf
)

add_test(NAME unit_tests COMMAND crafty_tests)

add_test(
    NAME failover
    COMMAND ${CMAKE_SOURCE_DIR}/tests/failover.sh $<TARGET_FILE_DIR:crafty_node>
)
set_tests_properties(failover PROPERTIES TIMEOUT 60)