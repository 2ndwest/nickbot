cmake_minimum_required(VERSION 3.24)
project(nickbot CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
# Pinned rather than tracking main: a branch name only re-fetches when CMake
# reconfigures, so plain rebuilds silently kept linking a stale cached copy.
FetchContent_Declare(libtouchstone GIT_REPOSITORY https://github.com/2ndwest/libtouchstone.git GIT_TAG 1a7519b80391982e13952348da9f0484e80b0ff5)
FetchContent_Declare(json_cpp GIT_REPOSITORY https://github.com/jart/json.cpp GIT_TAG main)
FetchContent_MakeAvailable(libtouchstone json_cpp)

find_package(dpp REQUIRED)
find_package(SQLite3 REQUIRED)

# CONFIGURE_DEPENDS: re-glob on every build so newly added command files are picked up without a manual reconfigure.
file(GLOB COMMAND_SOURCES CONFIGURE_DEPENDS commands/*.cc)
add_executable(bot bot.cc db.cc ${COMMAND_SOURCES})

target_link_libraries(bot libtouchstone::libtouchstone dpp::dpp SQLite::SQLite3 json)
target_include_directories(bot PRIVATE ${json_cpp_SOURCE_DIR} ${CMAKE_SOURCE_DIR})
target_compile_features(bot PRIVATE cxx_std_17)
