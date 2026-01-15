cmake_minimum_required(VERSION 3.20)
project(nextsearch LANGUAGES CXX)

# Use C++17 for this project
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define source and include directories
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

# Build command-line tools
add_executable(forwardindex ${SRC_DIR}/ForwardIndex.cpp)
add_executable(lexicon ${SRC_DIR}/lexicon.cpp)
add_executable(adddocument ${SRC_DIR}/AddDocument.cpp)

# Build API server executable with all required sources
add_executable(api_server
  ${SRC_DIR}/api_server.cpp
  ${SRC_DIR}/api_engine.cpp
  ${SRC_DIR}/api_autocomplete.cpp
  ${SRC_DIR}/api_segment.cpp
  ${SRC_DIR}/api_metadata.cpp
  ${SRC_DIR}/api_http.cpp
  ${SRC_DIR}/api_add_document.cpp
  ${SRC_DIR}/api_ai_overview.cpp
  ${SRC_DIR}/api_ai_summary.cpp
  ${SRC_DIR}/api_feedback.cpp
  ${SRC_DIR}/semantic_embedding.cpp
)

# Add include paths for each target
target_include_directories(forwardindex PRIVATE ${INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(lexicon PRIVATE ${INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(adddocument PRIVATE ${INCLUDE_DIR} ${CMAKE_SOURCE_DIR})
target_include_directories(api_server PRIVATE ${INCLUDE_DIR} ${CMAKE_SOURCE_DIR})

# Windows-only compile flags and libraries
if (WIN32)
  target_compile_definitions(api_server PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 CPPHTTPLIB_NO_MMAP)
  target_link_libraries(api_server PRIVATE ws2_32 iphlpapi winhttp)
endif()
