cmake_minimum_required(VERSION 3.14)
project(crafty VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

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