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

# Find OpenSSL for JWT authentication (REQUIRED)
# Set OpenSSL paths for MinGW with MSYS2
if(WIN32 AND MINGW)
  set(OPENSSL_ROOT "C:/msys64/mingw64")
  set(OPENSSL_INCLUDE "${OPENSSL_ROOT}/include")
  set(OPENSSL_LIB_DIR "${OPENSSL_ROOT}/lib")
  
  if(EXISTS "${OPENSSL_INCLUDE}/openssl/ssl.h")
    message(STATUS "OpenSSL found at ${OPENSSL_ROOT}")
    target_include_directories(api_server PRIVATE "${OPENSSL_INCLUDE}")
    target_link_libraries(api_server PRIVATE 
      "${OPENSSL_LIB_DIR}/libssl.dll.a"
      "${OPENSSL_LIB_DIR}/libcrypto.dll.a")
    target_compile_definitions(api_server PRIVATE NEXTSEARCH_HAVE_OPENSSL CPPHTTPLIB_OPENSSL_SUPPORT)
    message(STATUS "Admin authentication enabled")
  else()
    message(FATAL_ERROR "OpenSSL not found. Please install via MSYS2: pacman -S mingw-w64-x86_64-openssl")
  endif()
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(api_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(api_server PRIVATE NEXTSEARCH_HAVE_OPENSSL CPPHTTPLIB_OPENSSL_SUPPORT)
  message(STATUS "OpenSSL found - Admin authentication enabled")
endif()

# Windows-only compile flags and libraries
if (WIN32)
  target_compile_definitions(api_server PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 CPPHTTPLIB_NO_MMAP)
  target_link_libraries(api_server PRIVATE ws2_32 iphlpapi winhttp crypt32)
endif()
