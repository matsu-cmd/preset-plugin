cmake_minimum_required(VERSION 3.20)
project(preset_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(libpqxx CONFIG REQUIRED)
find_package(PostgreSQL REQUIRED)

add_executable(preset_cli
    src/main.cpp
    src/preset_repo.cpp
)

target_include_directories(preset_cli PRIVATE src)

target_link_libraries(preset_cli PRIVATE
    libpqxx::pqxx
    PostgreSQL::PostgreSQL
)
