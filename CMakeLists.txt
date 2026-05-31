cmake_minimum_required(VERSION 3.15)
project(DiscordStatusUpdater VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch nlohmann/json (header-only) via URL
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# Fetch WebView2 SDK from NuGet package
# (FetchContent_Populate is used because the NuGet package has no CMakeLists.txt)
set(WEBVIEW2_VERSION "1.0.2792.45")
cmake_policy(SET CMP0135 NEW)
FetchContent_Declare(
    webview2
    URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
cmake_policy(SET CMP0169 OLD)
FetchContent_Populate(webview2)
cmake_policy(SET CMP0169 NEW)

# Set WebView2 SDK paths based on architecture
set(WEBVIEW2_INCLUDE_DIR "${webview2_SOURCE_DIR}/build/native/include")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_LIB_DIR "${webview2_SOURCE_DIR}/build/native/x64")
else()
    set(WEBVIEW2_LIB_DIR "${webview2_SOURCE_DIR}/build/native/x86")
endif()

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/config.cpp
    src/discord_api.cpp
    src/token_extractor.cpp
    src/process_monitor.cpp
    src/tray_app.cpp
    src/single_instance.cpp
    src/worker.cpp
    src/setup_dialog.cpp
    src/webview_login.cpp
    res/app.rc
)

target_include_directories(${PROJECT_NAME} PRIVATE
    src
    res
    ${WEBVIEW2_INCLUDE_DIR}
)

# Prevent Windows.h min/max macros from conflicting with std::min/std::max
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(${PROJECT_NAME} PRIVATE
    nlohmann_json::nlohmann_json
    advapi32
    kernel32
    shell32
    user32
    ole32
    oleaut32
    wininet
    crypt32
    bcrypt
    shlwapi
    "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib"
)

# Copy config.json next to the executable after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config.json"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config.json"
)

# Copy WebView2Loader.dll next to the executable after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/WebView2Loader.dll"
)