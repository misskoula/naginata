# NAGINATA CLAP plugin: native CLAP build of the same DSP as the VST3 build in vst3/.
# Produces NAGINATA.clap. Trial expiry (unix seconds) is passed in by the build; 0 disables the lock.
cmake_minimum_required(VERSION 3.20)
project(NAGINATA_CLAP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(NAGINATA_TRIAL_EXPIRY "0" CACHE STRING "Unix time after which output is muted")

add_library(NAGINATA_CLAP MODULE naginata_clap.cpp)
set_target_properties(NAGINATA_CLAP PROPERTIES
    OUTPUT_NAME "NAGINATA"
    PREFIX ""
    SUFFIX ".clap")
target_include_directories(NAGINATA_CLAP PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(NAGINATA_CLAP PRIVATE NAGINATA_TRIAL_EXPIRY=${NAGINATA_TRIAL_EXPIRY}LL)
if(MINGW)
    target_link_options(NAGINATA_CLAP PRIVATE -static-libstdc++ -static-libgcc)
endif()
