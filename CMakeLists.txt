cmake_minimum_required(VERSION 3.20)
project(crypto_dashboard_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  cpp_httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.5.tar.gz
)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(cpp_httplib nlohmann_json)

add_executable(crypto_dashboard_cpp src/main.cpp)

target_link_libraries(crypto_dashboard_cpp PRIVATE httplib::httplib nlohmann_json::nlohmann_json)

target_compile_definitions(crypto_dashboard_cpp PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
