cmake_minimum_required(VERSION 3.15)

project(musicpp)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL CONFIG REQUIRED)
find_package(cppwinrt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(DISCORD_SDK_ROOT "${CMAKE_SOURCE_DIR}/discordsdk")
set(DISCORD_SDK_LIB_DIR "${DISCORD_SDK_ROOT}/lib/release")
set(DISCORD_SDK_BIN_DIR "${DISCORD_SDK_ROOT}/bin/release")
set(DISCORD_SDK_INCLUDE_DIR "${DISCORD_SDK_ROOT}/include")
set(DISCORD_LIB_PATH "${DISCORD_SDK_LIB_DIR}/discord_partner_sdk.lib")
set(DISCORD_SHARED_LIB "${DISCORD_SDK_BIN_DIR}/discord_partner_sdk.dll")

include_directories(./include)
include_directories(discordsdk)

set(RESOURCE_FILE resource.rc)

add_definitions(-D_HAS_STD_BYTE=0 -DNOMINMAX -DWIN32_LEAN_AND_MEAN)

add_executable(musicpp
        src/main.cpp
        src/mediaPlayer.cpp
        src/spotify.cpp
        src/imgur.cpp
        src/discordrp.cpp
        src/credhelper.cpp
        src/discordpp_impl.cpp
        ${RESOURCE_FILE}
        src/timeutils.cpp
        src/consoleutils.cpp
        src/stringutils.cpp
        src/utils.cpp
)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(LOG_LEVEL_DEBUG)
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(LOG_LEVEL_INFO)
endif()

set_target_properties(musicpp PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS")

target_link_libraries(musicpp PRIVATE Microsoft::CppWinRT CURL::libcurl windowsapp nlohmann_json::nlohmann_json ${DISCORD_LIB_PATH} Advapi32 leveldb::leveldb Shell32 spdlog::spdlog)
target_include_directories(musicpp PRIVATE ${DISCORD_SDK_INCLUDE_DIR})
target_compile_definitions(musicpp PRIVATE UNICODE _UNICODE)

# Copy Social SDK shared library to output directory, so it's available at runtime.
add_custom_command(TARGET musicpp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${DISCORD_SHARED_LIB}"
        $<TARGET_FILE_DIR:musicpp>;
)
