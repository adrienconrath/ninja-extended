cmake_minimum_required(VERSION 2.8)

project(ninja-extended)

find_package(Boost 1.49 COMPONENTS system thread REQUIRED)
find_package(Protobuf REQUIRED)

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -g -Wall -Wextra -Wno-deprecated -Wno-unused-parameter -fvisibility=hidden -pipe -Wno-missing-field-initializers -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC")

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS src/messages.proto)

include_directories(${Boost_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})

set(SOURCE
  ${PROTO_HDRS}
  ${PROTO_SRCS}
  src/background_processor.cc
  src/build.cc
  src/build_log.cc
  src/clean.cc
  src/client.cc
  src/comms.cc
  src/communicator.cc
  src/daemon.cc
  src/debug_flags.cc
  src/depfile_parser.cc
  src/deps_log.cc
  src/disk_interface.cc
  src/edit_distance.cc
  src/eval_env.cc
  src/file_monitor.cc
  src/graph.cc
  src/graphviz.cc
  src/lexer.cc
  src/line_printer.cc
  src/manifest_parser.cc
  src/metrics.cc
  src/ninja.cc
  src/processor.cc
  src/state.cc
  src/subprocess-posix.cc
  src/util.cc
  src/version.cc
)

add_executable(ninja ${SOURCE})
target_link_libraries(ninja ${Boost_LIBRARIES} pthread ${PROTOBUF_LIBRARIES})
