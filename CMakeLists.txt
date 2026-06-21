cmake_minimum_required(VERSION 3.16)
project(proto_test CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
  GIT_TAG v35.1
  GIT_SHALLOW TRUE
)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(protobuf)

# protobuf_generate() is normally pulled in via find_package(protobuf CONFIG),
# but FetchContent builds protobuf as a subdirectory instead, so we include
# the function definition directly from the fetched source tree.
include(${protobuf_SOURCE_DIR}/cmake/protobuf-generate.cmake)

# chat_proto holds the generated chat.pb.{h,cc} so both proto_test and
# proto_bench can link the same compiled protobuf code instead of running
# protoc twice.
add_library(chat_proto STATIC)
# protobuf's own cmake/libprotobuf-lite.cmake links utf8_validity PRIVATE
# (libprotobuf.cmake links it PUBLIC), so its include dir for
# utf8_validity.h doesn't propagate from libprotobuf-lite alone.
target_link_libraries(chat_proto PUBLIC protobuf::libprotobuf-lite utf8_validity)

# protobuf_generate() invokes protoc with --cpp_out pointed at this directory
# but does not create it itself, so it must exist before the build runs.
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)

protobuf_generate(
  TARGET chat_proto
  PROTOS ${CMAKE_CURRENT_SOURCE_DIR}/proto/chat.proto
  IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/proto
  PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_include_directories(chat_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(proto_test src/main.cpp src/message_fixtures.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp src/json_rapidjson_fixtures.cpp)
target_link_libraries(proto_test PRIVATE chat_proto)

FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.9.1
  GIT_SHALLOW TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(proto_bench src/bench.cpp src/message_fixtures.cpp src/alloc_counter.cpp src/sbe_message_fixtures.cpp src/json_nlohmann_fixtures.cpp src/json_rapidjson_fixtures.cpp)
target_link_libraries(proto_bench PRIVATE chat_proto benchmark::benchmark)

find_package(Java REQUIRED COMPONENTS Runtime)

set(SBE_TOOL_VERSION 1.38.1)
set(SBE_JAR ${CMAKE_CURRENT_BINARY_DIR}/sbe-all-${SBE_TOOL_VERSION}.jar)
file(DOWNLOAD
  https://repo1.maven.org/maven2/uk/co/real-logic/sbe-all/${SBE_TOOL_VERSION}/sbe-all-${SBE_TOOL_VERSION}.jar
  ${SBE_JAR}
  EXPECTED_HASH SHA1=ef7dd43a54a0269854ac2a296c2f6ba25edbaeff
)

set(SBE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-sbe)
file(MAKE_DIRECTORY ${SBE_GENERATED_DIR})

set(SBE_GENERATED_HEADERS
  ${SBE_GENERATED_DIR}/im_chat_sbe/ConversationType.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/GroupSizeEncoding.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MergedForwardChatMessage.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MessageHeader.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/MessageStatus.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/TextChatMessage.h
  ${SBE_GENERATED_DIR}/im_chat_sbe/VarStringEncoding.h
)

add_custom_command(
  OUTPUT ${SBE_GENERATED_HEADERS}
  COMMAND ${Java_JAVA_EXECUTABLE}
          --add-opens java.base/jdk.internal.misc=ALL-UNNAMED
          -Dsbe.output.dir=${SBE_GENERATED_DIR}
          -Dsbe.target.language=Cpp
          -Dsbe.errorLog=yes
          -jar ${SBE_JAR}
          ${CMAKE_CURRENT_SOURCE_DIR}/schema/chat.sbe.xml
  DEPENDS ${SBE_JAR} ${CMAKE_CURRENT_SOURCE_DIR}/schema/chat.sbe.xml
  COMMENT "Generating SBE C++ codecs from schema/chat.sbe.xml"
)
add_custom_target(generate_chat_sbe DEPENDS ${SBE_GENERATED_HEADERS})

add_library(chat_sbe INTERFACE)
target_include_directories(chat_sbe INTERFACE ${SBE_GENERATED_DIR})
add_dependencies(chat_sbe generate_chat_sbe)

target_link_libraries(proto_test PRIVATE chat_sbe)
target_link_libraries(proto_bench PRIVATE chat_sbe)

# ---- Phase 10: JSON library shootout (nlohmann/json, RapidJSON, yyjson, cJSON) ----
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG v1.1.0
  GIT_SHALLOW TRUE
)
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidjson)
# RapidJSON's own CMakeLists.txt does not export a consumable target (its
# include_directories() call is scoped to its own subdirectory only), so we
# add its include path manually below.

FetchContent_Declare(
  yyjson
  GIT_REPOSITORY https://github.com/ibireme/yyjson.git
  GIT_TAG 0.12.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(yyjson)

FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG v1.7.19
  GIT_SHALLOW TRUE
)
set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cjson)
# cJSON's CMakeLists.txt does not call target_include_directories() on its
# own target, so we add ${cjson_SOURCE_DIR} manually below (cJSON.h sits at
# the repo root, so the include is "cJSON.h", not "cjson/cJSON.h").

target_include_directories(proto_test PRIVATE ${rapidjson_SOURCE_DIR}/include ${cjson_SOURCE_DIR})
target_include_directories(proto_bench PRIVATE ${rapidjson_SOURCE_DIR}/include ${cjson_SOURCE_DIR})
target_link_libraries(proto_test PRIVATE nlohmann_json::nlohmann_json yyjson cjson)
target_link_libraries(proto_bench PRIVATE nlohmann_json::nlohmann_json yyjson cjson)
