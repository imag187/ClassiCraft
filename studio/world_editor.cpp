#include "imgui.h"
#include "imgui_stdlib.h"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using Json = nlohmann::json;
namespace fs = std::filesystem;
namespace {
constexpr int W = 16, H = 8;
Json project;
bool initialized = false, dirty = false, loadFailed = false;
std::string message;
int tileIndex = 0, mapIndex = 0, selectedLayer = 0;
float paint[4] = {.35f, .65f, .3f, 1.f};

Json Tile(const char* id, const char* name, ImU32 color) {
    Json pixels = Json::array();
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        bool inside = std::abs((x + .5f - W / 2.f) / (W / 2.f)) +
                      std::abs((y + .5f - H / 2.f) / (H / 2.f)) <= 1.f;
        pixels.push_back(inside ? color : 0u);
    }
    return {{"id", id}, {"name", name}, {"pixels", pixels}};
}

Json DefaultProject() {
    Json tiles = Json::array({Tile("grass", "Grass", IM_COL32(73, 139, 71, 255)),
        Tile("snow", "Snow", IM_COL32(210, 227, 235, 255)),
        Tile("path", "Path", IM_COL32(155, 125, 85, 255))});
    Json cells = Json::array();
    for (int y = 0; y < 20; ++y) for (int x = 0; x < 20; ++x)
        cells.push_back({{"x", x}, {"y", y}, {"z", 0}, {"tile", "grass"}, {"collision", false}});
    Json map = {{"id", "test-room"}, {"name", "Test room"}, {"width", 20}, {"height", 20}, {"cells", cells}};
    return {{"version", 1}, {"tiles", tiles}, {"maps", Json::array({map})},
        {"world", {{"name", "ClassiCraft test world"},
            {"maps", Json::array({{{"map", "test-room"}, {"x", 0}, {"y", 0}, {"z", 0}}})},
            {"objects", Json::array({{{"name", "Player spawn"}, {"kind", "spawn"}, {"x", 3}, {"y", 3}, {"z", 0}, {"collision", false}},
                {{"name", "Training dummy"}, {"kind", "enemy"}, {"x", 8}, {"y", 8}, {"z", 0}, {"collision", true}},
                {{"name", "Test wall"}, {"kind", "object"}, {"x", 10}, {"y", 8}, {"z", 0}, {"collision", true}}})}}}};
}

void Load(const fs::path& path) {
    try {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("Could not open world project.");
        Json next = Json::parse(in);
        if (next.at("version") != 1 || !next.at("tiles").is_array() || !next.at("maps").is_array() || !next.at("world").is_object())
            throw std::runtime_error("Unsupported world project format.");
        for (const auto& tile : next.at("tiles")) {
            if (!tile.at("id").is_string() || !tile.at("name").is_string() || !tile.at("pixels").is_array() || tile.at("pixels").size() != W * H)
                throw std::runtime_error("Invalid tile definition.");
            for (const auto& pixel : tile.at("pixels")) if (!pixel.is_number_unsigned()) throw std::runtime_error("Invalid tile pixel.");
        }
        for (const auto& map : next.at("maps")) {
            if (!map.at("id").is_string() || !map.at("name").is_string() || !map.at("cells").is_array() ||
                !map.at("width").is_number_integer() || !map.at("height").is_number_integer() ||
                map.at("width").get<int>() < 1 || map.at("width").get<int>() > 64 || map.at("height").get<int>() < 1 || map.at("height").get<int>() > 64)
                throw std::runtime_error("Invalid map definition.");
            for (const auto& c : map.at("cells")) {
                c.at("x").get<int>(); c.at("y").get<int>(); c.at("z").get<int>();
                c.at("tile").get<std::string>(); c.at("collision").get<bool>();
            }
        }
        auto& world = next.at("world");
        world.at("name").get<std::string>();
        if (!world.at("maps").is_array() || !world.at("objects").is_array()) throw std::runtime_error("Invalid world.");
        for (const auto& m : world.at("maps")) { m.at("map").get<std::string>(); m.at("x").get<int>(); m.at("y").get<int>(); m.at("z").get<int>(); }
        for (const auto& o : world.at("objects")) { o.at("name").get<std::string>(); o.at("kind").get<std::string>(); o.at("x").get<int>(); o.at("y").get<int>(); o.at("z").get<int>(); o.at("collision").get<bool>(); }
        project = std::move(next); tileIndex = mapIndex = 0; dirty = false; loadFailed = false;
        message = "World project loaded.";
    } catch (const std::exception& e) { loadFailed = true; message = std::string("Load failed; save disabled: ") + e.what(); }
}

void Save(const fs::path& path) {
    try {
        fs::path temp = path; temp += L".tmp";
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << project.dump(2) << '\n'; out.close();
        #ifdef _WIN32
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Could not replace project file.");
        #else
        fs::rename(temp, path);
        #endif
        dirty = false; message = "Saved world-project.json.";
    } catch (const std::exception& e) { message = std::string("Save failed: ") + e.what(); }
}

Json* FindTile(const std::string& id) {
    for (auto& tile : project["tiles"]) if (tile["id"] == id) return &tile;
    return nullptr;
}
void TilePicture(ImDrawList* draw, ImVec2 pos, float scale, const Json& tile) {
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const auto color = tile["pixels"][y * W + x].get<ImU32>();
        if (color >> 24) draw->AddRectFilled({pos.x + x * scale, pos.y + y * scale},
            {pos.x + (x + 1) * scale, pos.y + (y + 1) * scale}, color);
    }
}

void TileEditor() {
    auto& tiles = project["tiles"];
    if (ImGui::Button("New tile")) {
        int n = 1; std::string id;
        do { id = "tile-" + std::to_string(n++); } while (FindTile(id));
        tiles.push_back(Tile(id.c_str(), "New tile", IM_COL32(125, 125, 125, 255)));
        tileIndex = static_cast<int>(tiles.size()) - 1; dirty = true;
    }
    if (tiles.empty()) return;
    tileIndex = std::clamp(tileIndex, 0, static_cast<int>(tiles.size()) - 1);
    if (ImGui::BeginCombo("Tile", tiles[tileIndex]["name"].get_ref<const std::string&>().c_str())) {
        for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(tiles[i]["name"].get_ref<const std::string&>().c_str(), i == tileIndex)) tileIndex = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    auto& tile = tiles[tileIndex];
    if (ImGui::InputText("Name", &tile["name"].get_ref<std::string&>())) dirty = true;
    ImGui::Text("ID: %s", tile["id"].get_ref<const std::string&>().c_str());
    ImGui::ColorEdit4("Paint color", paint);
    ImGui::TextWrapped("Paint pixels with left mouse; erase with right mouse. Transparent pixels reveal lower layers.");
    constexpr float scale = 24.f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("tile canvas", {W * scale, H * scale});
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, {pos.x + W * scale, pos.y + H * scale}, IM_COL32(38, 43, 52, 255));
    TilePicture(draw, pos, scale, tile);
    for (int x = 0; x <= W; ++x) draw->AddLine({pos.x + x * scale, pos.y}, {pos.x + x * scale, pos.y + H * scale}, IM_COL32(95, 100, 110, 110));
    for (int y = 0; y <= H; ++y) draw->AddLine({pos.x, pos.y + y * scale}, {pos.x + W * scale, pos.y + y * scale}, IM_COL32(95, 100, 110, 110));
    if (ImGui::IsItemHovered() && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
        auto mouse = ImGui::GetIO().MousePos;
        int x = static_cast<int>((mouse.x - pos.x) / scale), y = static_cast<int>((mouse.y - pos.y) / scale);
        if (x >= 0 && x < W && y >= 0 && y < H) {
            ImU32 value = ImGui::IsMouseDown(1) ? 0u : ImGui::ColorConvertFloat4ToU32({paint[0], paint[1], paint[2], paint[3]});
            if (tile["pixels"][y * W + x] != value) { tile["pixels"][y * W + x] = value; dirty = true; }
        }
    }
}

void MapEditor() {
    auto& maps = project["maps"]; auto& tiles = project["tiles"];
    static int newWidth = 20, newHeight = 20;
    ImGui::SetNextItemWidth(100); ImGui::InputInt("New map width", &newWidth);
    ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputInt("New map height", &newHeight);
    newWidth = std::clamp(newWidth, 1, 64); newHeight = std::clamp(newHeight, 1, 64);
    if (ImGui::Button("New map")) {
        int n = 1; std::string id; bool exists;
        do { id = "map-" + std::to_string(n++); exists = false; for (const auto& m : maps) if (m["id"] == id) exists = true; } while (exists);
        maps.push_back({{"id", id}, {"name", "New map"}, {"width", newWidth}, {"height", newHeight}, {"cells", Json::array()}});
        mapIndex = static_cast<int>(maps.size()) - 1; dirty = true;
    }
    if (maps.empty() || tiles.empty()) { ImGui::TextUnformatted("Create a map and a tile first."); return; }
    mapIndex = std::clamp(mapIndex, 0, static_cast<int>(maps.size()) - 1);
    tileIndex = std::clamp(tileIndex, 0, static_cast<int>(tiles.size()) - 1);
    if (ImGui::BeginCombo("Map", maps[mapIndex]["name"].get_ref<const std::string&>().c_str())) {
        for (int i = 0; i < static_cast<int>(maps.size()); ++i) { ImGui::PushID(i); if (ImGui::Selectable(maps[i]["name"].get_ref<const std::string&>().c_str(), i == mapIndex)) mapIndex = i; ImGui::PopID(); } ImGui::EndCombo();
    }
    auto& map = maps[mapIndex];
    if (ImGui::InputText("Map name", &map["name"].get_ref<std::string&>())) dirty = true;
    int width = map["width"], height = map["height"];
    ImGui::Text("Map dimensions: %d x %d", width, height);
    ImGui::TextUnformatted("Create a new map to preserve placed tiles; resizing is not yet available.");
    if (ImGui::BeginCombo("Paint tile", tiles[tileIndex]["name"].get_ref<const std::string&>().c_str())) {
        for (int i = 0; i < static_cast<int>(tiles.size()); ++i) { ImGui::PushID(i); if (ImGui::Selectable(tiles[i]["name"].get_ref<const std::string&>().c_str(), i == tileIndex)) tileIndex = i; ImGui::PopID(); } ImGui::EndCombo();
    }
    ImGui::SliderInt("Z layer", &selectedLayer, 0, 7);
    static bool collision = false;
    ImGui::Checkbox("Paint collision", &collision);
    ImGui::TextWrapped("Left click to place; right click to erase this layer. Mouse wheel scrolls the canvas.");
    ImGui::BeginChild("map scroll", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    constexpr float s = 2.f, tw = W * s, th = H * s;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    const float canvasWidth = (width + height) * tw / 2 + 40, canvasHeight = (width + height) * th / 2 + 40;
    ImGui::InvisibleButton("map canvas", {canvasWidth, canvasHeight});
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + canvasWidth, origin.y + canvasHeight}, IM_COL32(30, 35, 43, 255));
    const float ox = origin.x + height * tw / 2 + 20, oy = origin.y + 10;
    auto cells = map["cells"];
    std::sort(cells.begin(), cells.end(), [](const Json& a, const Json& b) {
        int az = a["z"], bz = b["z"]; if (az != bz) return az < bz;
        return a["x"].get<int>() + a["y"].get<int>() < b["x"].get<int>() + b["y"].get<int>();
    });
    for (const auto& c : cells) if (auto* tile = FindTile(c["tile"])) {
        float x = ox + (c["x"].get<int>() - c["y"].get<int>()) * tw / 2 - tw / 2;
        float y = oy + (c["x"].get<int>() + c["y"].get<int>()) * th / 2 - c["z"].get<int>() * 2;
        TilePicture(draw, {x, y}, s, *tile);
        if (c["collision"].get<bool>()) draw->AddCircleFilled({x + tw / 2, y + th / 2}, 2.f, IM_COL32(255, 100, 90, 255));
    }
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        float cx = ox + (x - y) * tw / 2;
        float cy = oy + (x + y) * th / 2 + th / 2 - selectedLayer * 2;
        draw->AddQuad({cx, cy - th / 2}, {cx + tw / 2, cy}, {cx, cy + th / 2}, {cx - tw / 2, cy}, IM_COL32(15, 20, 25, 85));
    }
    if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1))) {
        auto m = ImGui::GetIO().MousePos;
        float dx = (m.x - ox) / (tw / 2), dy = (m.y - oy - th / 2 + selectedLayer * 2) / (th / 2);
        int x = static_cast<int>(std::floor((dx + dy) / 2 + .5f));
        int y = static_cast<int>(std::floor((dy - dx) / 2 + .5f));
        if (x >= 0 && x < width && y >= 0 && y < height) {
            auto& target = map["cells"];
            for (auto it = target.begin(); it != target.end();) {
                if ((*it)["x"] == x && (*it)["y"] == y && (*it)["z"] == selectedLayer) it = target.erase(it); else ++it;
            }
            if (ImGui::IsMouseClicked(0)) target.push_back({{"x", x}, {"y", y}, {"z", selectedLayer}, {"tile", tiles[tileIndex]["id"]}, {"collision", collision}});
            dirty = true;
        }
    }
    ImGui::EndChild();
}

void WorldEditor() {
    auto& world = project["world"]; auto& maps = project["maps"];
    static int placingObject = -1, placingMap = -1;
    if (ImGui::InputText("World name", &world["name"].get_ref<std::string&>())) dirty = true;
    ImGui::TextWrapped("Place maps in world coordinates. Add spawns, enemies, and objects with positions and collision flags.");
    if (!maps.empty() && ImGui::Button("Add selected map")) {
        mapIndex = std::clamp(mapIndex, 0, static_cast<int>(maps.size()) - 1);
        world["maps"].push_back({{"map", maps[mapIndex]["id"]}, {"x", 0}, {"y", 0}, {"z", 0}}); dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add object")) {
        world["objects"].push_back({{"name", "New object"}, {"kind", "object"}, {"x", 0}, {"y", 0}, {"z", 0}, {"collision", false}}); dirty = true;
    }
    ImGui::BeginChild("world records", {350, 0}, ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Placed maps");
    int removeMap = -1;
    for (int i = 0; i < static_cast<int>(world["maps"].size()); ++i) {
        auto& m = world["maps"][i]; ImGui::PushID(i);
        ImGui::Text("Map: %s", m["map"].get_ref<const std::string&>().c_str()); ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeMap = i;
        ImGui::SameLine(); if (ImGui::SmallButton("Place on canvas")) { placingMap = i; placingObject = -1; }
        for (const char* key : {"x", "y", "z"}) { int v = m[key]; ImGui::SetNextItemWidth(70); if (ImGui::InputInt(key, &v, 0, 0)) { m[key] = v; dirty = true; } ImGui::SameLine(); } ImGui::NewLine(); ImGui::PopID();
    }
    if (removeMap >= 0) { world["maps"].erase(removeMap); placingMap = -1; dirty = true; }
    ImGui::SeparatorText("World objects");
    int removeObject = -1;
    for (int i = 0; i < static_cast<int>(world["objects"].size()); ++i) {
        auto& o = world["objects"][i]; ImGui::PushID(10000 + i);
        if (ImGui::InputText("Name", &o["name"].get_ref<std::string&>())) dirty = true;
        if (ImGui::InputText("Kind", &o["kind"].get_ref<std::string&>())) dirty = true;
        for (const char* key : {"x", "y", "z"}) { int v = o[key]; ImGui::SetNextItemWidth(70); if (ImGui::InputInt(key, &v, 0, 0)) { o[key] = v; dirty = true; } ImGui::SameLine(); }
        ImGui::NewLine();
        bool collision = o["collision"]; if (ImGui::Checkbox("Collision", &collision)) { o["collision"] = collision; dirty = true; }
        if (ImGui::SmallButton("Place on canvas")) { placingObject = i; placingMap = -1; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove object")) removeObject = i;
        ImGui::Separator(); ImGui::PopID();
    }
    if (removeObject >= 0) { world["objects"].erase(removeObject); placingObject = -1; dirty = true; }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("world canvas area", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(placingMap >= 0 || placingObject >= 0 ? "Click to place selected record; right-click cancels." : "Choose Place on canvas beside a map or object.");
    constexpr float tw = 32.f, th = 16.f, scale = 2.f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    const float size = 2200.f, ox = origin.x + 350.f, oy = origin.y + 80.f;
    ImGui::InvisibleButton("world canvas", {size, size});
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, {origin.x + size, origin.y + size}, IM_COL32(30, 35, 43, 255));
    for (const auto& placement : world["maps"]) {
        for (const auto& map : maps) if (map["id"] == placement["map"]) {
            auto cells = map["cells"];
            std::sort(cells.begin(), cells.end(), [](const Json& a, const Json& b) {
                int az = a["z"], bz = b["z"]; if (az != bz) return az < bz;
                return a["x"].get<int>() + a["y"].get<int>() < b["x"].get<int>() + b["y"].get<int>();
            });
            for (const auto& c : cells) if (auto* tile = FindTile(c["tile"])) {
                int x = c["x"].get<int>() + placement["x"].get<int>();
                int y = c["y"].get<int>() + placement["y"].get<int>();
                int z = c["z"].get<int>() + placement["z"].get<int>();
                TilePicture(draw, {ox + (x - y) * tw / 2 - tw / 2, oy + (x + y) * th / 2 - z * 2}, scale, *tile);
            }
        }
    }
    for (const auto& object : world["objects"]) {
        int x = object["x"], y = object["y"], z = object["z"];
        ImVec2 p{ox + (x - y) * tw / 2, oy + (x + y) * th / 2 + th / 2 - z * 2};
        draw->AddCircleFilled(p, 5.f, object["collision"].get<bool>() ? IM_COL32(255, 115, 100, 255) : IM_COL32(120, 200, 255, 255));
        draw->AddText({p.x + 6, p.y - 8}, IM_COL32_WHITE, object["name"].get_ref<const std::string&>().c_str());
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) { placingMap = placingObject = -1; }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) && (placingObject >= 0 || placingMap >= 0)) {
        Json& record = placingObject >= 0 ? world["objects"][placingObject] : world["maps"][placingMap];
        auto mouse = ImGui::GetIO().MousePos;
        float dx = (mouse.x - ox) / (tw / 2), dy = (mouse.y - oy - th / 2 + record["z"].get<int>() * 2) / (th / 2);
        record["x"] = static_cast<int>(std::floor((dx + dy) / 2 + .5f));
        record["y"] = static_cast<int>(std::floor((dy - dx) / 2 + .5f));
        placingObject = placingMap = -1; dirty = true;
    }
    ImGui::EndChild();
}
}

void DrawWorldEditor(const fs::path& directory) {
    const auto path = directory / "world-project.json";
    if (!initialized) { project = DefaultProject(); initialized = true; if (fs::exists(path)) Load(path); }
    ImGui::SameLine();
    ImGui::BeginDisabled(loadFailed);
    if (ImGui::Button("Save world")) Save(path);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(dirty || !fs::exists(path));
    if (ImGui::Button("Reload world")) Load(path);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Text("%s%s", message.c_str(), dirty ? "  (unsaved changes)" : "");
    if (ImGui::BeginTabBar("world tools")) {
        if (ImGui::BeginTabItem("Tile editor")) { TileEditor(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Map editor")) { MapEditor(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("World editor")) { WorldEditor(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}
