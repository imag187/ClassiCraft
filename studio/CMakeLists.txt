cmake_minimum_required(VERSION 3.24)
project(ClassiCraftStudio LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui json)

add_executable(ClassiCraftStudio WIN32 main.cpp editor.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_compile_features(ClassiCraftStudio PRIVATE cxx_std_20)
set_property(TARGET ClassiCraftStudio PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_include_directories(ClassiCraftStudio PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${imgui_SOURCE_DIR}/misc/cpp)
target_sources(ClassiCraftStudio PRIVATE ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_link_libraries(ClassiCraftStudio PRIVATE nlohmann_json::nlohmann_json d3d11 windowscodecs ole32)
add_custom_command(TARGET ClassiCraftStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/example.json" "$<TARGET_FILE_DIR:ClassiCraftStudio>/example.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ClassiCraftStudio>/assets")
