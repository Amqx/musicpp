cmake_minimum_required(VERSION 3.15)

project(musicpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (USE_STATIC_LINKING)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBS OFF)
else ()
    set(BUILD_SHARED_LIBS ON)
endif ()

find_package(CURL CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(LibXml2 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
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
set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/release")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/release")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")
set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")

include_directories(include)
include_directories(discordsdk)
include_directories(resources)

set(RESOURCE_FILE src/resource.rc)

add_executable(musicpp
        src/main.cpp
        src/mediaPlayer.cpp
        src/spotify.cpp
        src/imgur.cpp
        src/discordrp.cpp
        src/credhelper.cpp
        ${RESOURCE_FILE}
        src/timeutils.cpp
        src/consoleutils.cpp
        src/stringutils.cpp
        src/utils.cpp
        src/amscraper.cpp
        src/lfm.cpp
        src/m3u8_internals.cpp
        src/m3u8.cpp
        src/musicpp.cpp
)

target_compile_definitions(musicpp PRIVATE -D_HAS_STD_BYTE=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_USE_64BIT_TIME_T)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(LOG_LEVEL_DEBUG)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(LOG_LEVEL_INFO)
endif()

set_target_properties(musicpp PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")
target_link_libraries(musicpp PRIVATE Microsoft::CppWinRT CURL::libcurl windowsapp nlohmann_json::nlohmann_json ${DISCORD_LIB_PATH} Advapi32 leveldb::leveldb Shell32 spdlog::spdlog LibXml2::LibXml2 PkgConfig::LIBAV)
target_include_directories(musicpp PRIVATE ${DISCORD_SDK_INCLUDE_DIR})
target_compile_definitions(musicpp PRIVATE UNICODE _UNICODE)

# Copy Social SDK shared library to output directory, so it's available at runtime.
add_custom_command(TARGET musicpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DISCORD_SHARED_LIB}"
        $<TARGET_FILE_DIR:musicpp>;
)
