cmake_minimum_required(VERSION 3.20)
project(cord19_search_cpp LANGUAGES CXX)

# Use C++17 for this project
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build command-line tools
add_executable(forwardindex   ForwardIndex.cpp)
add_executable(lexicon        lexicon.cpp)
add_executable(adddocument    AddDocument.cpp)

# Build API server executable with all required sources
add_executable(api_server
  api/api_server.cpp
  api/api_engine.cpp
  api/api_autocomplete.cpp
  api/api_segment.cpp
  api/api_metadata.cpp
  api/api_http.cpp
  api/api_add_document.cpp
  semantic_embedding.cpp
)

# Add include paths for each target
target_include_directories(forwardindex  PRIVATE .)
target_include_directories(lexicon       PRIVATE .)
target_include_directories(adddocument   PRIVATE .)
target_include_directories(api_server    PRIVATE . api)

# Windows-only compile flags and libraries
if (WIN32)
  target_compile_definitions(api_server PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 CPPHTTPLIB_NO_MMAP)
  target_link_libraries(api_server PRIVATE ws2_32 iphlpapi)
endif()
