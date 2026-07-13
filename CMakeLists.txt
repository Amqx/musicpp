cmake_minimum_required(VERSION 4.2)

project(musicpp)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(LibXml2 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Catch2 CONFIG REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
        libavdevice
        libavfilter
        libavformat
        libavcodec
        libswresample
        libswscale
        libavutil
)

# discordsdk config
set(DISCORD_SDK_ROOT "${CMAKE_SOURCE_DIR}/discordsdk")
if(CMAKE_BUILD_TYPE MATCHES "Debug")
    set(DISCORD_SDK_MODE "debug")
else()
    set(DISCORD_SDK_MODE "release")
endif()

set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/${DISCORD_SDK_MODE}")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/${DISCORD_SDK_MODE}")
set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")

include_directories(include)
include_directories(discordsdk)

#include_directories(resources)
#set(RESOURCE_FILE src/resource.rc)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(musicpp
        ${SOURCES}
)

target_compile_definitions(musicpp PRIVATE -D_HAS_STD_BYTE=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_USE_64BIT_TIME_T)

#set_target_properties(musicpp PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
target_link_libraries(musicpp PRIVATE Microsoft::CppWinRT CURL::libcurl windowsapp nlohmann_json::nlohmann_json ${DISCORD_LIB_PATH} Advapi32 leveldb::leveldb Shell32 spdlog::spdlog LibXml2::LibXml2 PkgConfig::LIBAV)
target_include_directories(musicpp PRIVATE ${DISCORD_SDK_INCLUDE_DIR})
target_compile_definitions(musicpp PRIVATE UNICODE _UNICODE)

# spdlog: strip trace/debug in Release, keep everything otherwise.
target_compile_definitions(musicpp PRIVATE
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
        $<$<NOT:$<CONFIG:Release>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>)

# Copy Social SDK shared library to output directory, so it's available at runtime.
add_custom_command(TARGET musicpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DISCORD_SHARED_LIB}"
        $<TARGET_FILE_DIR:musicpp>;
)

# Tests
enable_testing()

file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
add_executable(musicpp_tests
        ${TEST_SOURCES}
        src/metadata/cache.cpp
        src/metadata/cache_codec.cpp
        src/types/track.cpp
)

target_compile_definitions(musicpp_tests PRIVATE
        -D_HAS_STD_BYTE=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_USE_64BIT_TIME_T UNICODE _UNICODE)
target_link_libraries(musicpp_tests PRIVATE Catch2::Catch2WithMain leveldb::leveldb Shell32)

