cmake_minimum_required(VERSION 3.24)
project(ClassiCraft LANGUAGES CXX)
include(FetchContent)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
FetchContent_Declare(json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui json)

add_library(ClassiCraftGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(ClassiCraftGui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_features(ClassiCraftGui PUBLIC cxx_std_20)
target_link_libraries(ClassiCraftGui PUBLIC d3d11)

add_executable(ClassiCraftStudio WIN32 main.cpp editor.cpp world_editor.cpp ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(ClassiCraftStudio PRIVATE ${imgui_SOURCE_DIR}/misc/cpp)
target_link_libraries(ClassiCraftStudio PRIVATE ClassiCraftGui nlohmann_json::nlohmann_json windowscodecs ole32)

add_executable(ClassiCraftGame WIN32 main.cpp ../client/native/game.cpp ../client/native/connection.cpp)
target_compile_definitions(ClassiCraftGame PRIVATE CLASSICRAFT_GAME NOMINMAX)
target_link_libraries(ClassiCraftGame PRIVATE ClassiCraftGui nlohmann_json::nlohmann_json winhttp bcrypt shell32 ole32 uuid)

set_property(TARGET ClassiCraftGui ClassiCraftStudio ClassiCraftGame PROPERTY MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_custom_command(TARGET ClassiCraftStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/example.json" "$<TARGET_FILE_DIR:ClassiCraftStudio>/example.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:ClassiCraftStudio>/assets")
