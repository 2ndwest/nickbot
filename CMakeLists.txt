cmake_minimum_required(VERSION 3.24)
project(nickbot CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(libtouchstone GIT_REPOSITORY https://github.com/2ndwest/libtouchstone.git GIT_TAG main)
FetchContent_MakeAvailable(libtouchstone)

find_package(dpp REQUIRED)

add_executable(bot bot.cc)

target_link_libraries(bot libtouchstone::libtouchstone dpp::dpp)
target_compile_features(bot PRIVATE cxx_std_17)
