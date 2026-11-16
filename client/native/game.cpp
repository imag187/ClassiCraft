#include "connection.h"
#include <windows.h>
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace {
using game::Json;
constexpr float tileWidth = 48.f, tileHeight = 24.f, layerHeight = 8.f;
constexpr auto gold = IM_COL32(222, 183, 114, 255);
constexpr auto shadow = IM_COL32(10, 12, 17, 155);

struct Point {
    float x{}, y{};
};
struct Actor {
    Point position;
    Point visual;
    bool moving{};
    bool back{};
    bool left{};
};
struct Cell {
    Point center;
    std::string tile;
    bool blocked{};
    int z{};
};
using Tile = std::array<ImU32, 128>;

constexpr std::string_view ranger[]{
    "......####......", ".....#1111#.....", "....#112211#....", "....#122221#....",
    "....#135531#....", "....#134431#....", ".....#344#......", "....##222##.....",
    "...#3222233#....", "..#332222333#...", "..#132772231#...", "..#132882231#...",
    "..#136666231#...", "..#432112234#...", "...#3222223#....", "....#22122#.....",
    "....#11#11#.....", "....#11#11#.....", "....#66#66#.....", "...####.####...."
};
constexpr std::string_view dummy[]{
    ".....######.....", "....#aaaaaa#....", "...#abbbbaaa#...", "...#abbaabaa#...",
    "...#abaaaaaa#...", "....#aaaaaa#....", ".....######.....", "..##.#cccc#.##..",
    ".#ad##dddd##da#.", ".#addddccdddda#.", "..##addccdda##..", "....#ddccdd#....",
    "....#deeeed#....", ".....#dccd#.....", "......#cc#......", "......#cc#......",
    "......#cc#......", ".....#cccc#.....", "...##cccccc##...", "...##########..."
};

ImU32 SpriteColor(char pixel, bool back, bool remote) {
    switch (pixel) {
    case '#': return IM_COL32(25, 25, 33, 255);
    case '1': return remote ? IM_COL32(61, 64, 110, 255) : IM_COL32(37, 78, 77, 255);
    case '2': return remote ? IM_COL32(107, 112, 162, 255) : IM_COL32(72, 130, 112, 255);
    case '3': return remote ? IM_COL32(146, 145, 198, 255) : IM_COL32(119, 169, 135, 255);
    case '4': return back ? IM_COL32(37, 78, 77, 255) : IM_COL32(211, 158, 115, 255);
    case '5': return back ? IM_COL32(72, 130, 112, 255) : IM_COL32(46, 40, 46, 255);
    case '6': return IM_COL32(117, 75, 57, 255);
    case '7': return IM_COL32(235, 192, 97, 255);
    case '8': return IM_COL32(166, 117, 60, 255);
    case 'a': return IM_COL32(195, 148, 82, 255);
    case 'b': return IM_COL32(241, 197, 119, 255);
    case 'c': return IM_COL32(122, 83, 52, 255);
    case 'd': return IM_COL32(167, 87, 65, 255);
    case 'e': return IM_COL32(229, 150, 89, 255);
    default: return 0;
    }
}

void DrawCharacter(ImDrawList& draw, ImVec2 feet, bool trainingDummy, const Actor& actor, bool remote) {
    constexpr float scale = 2.f;
    draw.AddEllipseFilled(feet, {17, 7}, IM_COL32(10, 12, 17, 60));
    draw.AddEllipseFilled(feet, {12, 4}, shadow);
    const auto& rows = trainingDummy ? dummy : ranger;
    const int step = actor.moving ? static_cast<int>(ImGui::GetTime() * 9) % 2 : -1;
    for (int y = 0; y < 20; ++y) for (int x = 0; x < 16; ++x) {
        const auto color = SpriteColor(rows[y][actor.left ? 15 - x : x], actor.back, remote);
        if (!color) continue;
        const float lift = y >= 16 && step == x / 8 ? scale : 0.f;
        const ImVec2 corner{feet.x + (x - 8) * scale, feet.y + (y - 20) * scale - lift};
        draw.AddRectFilled(corner, {corner.x + scale, corner.y + scale}, color);
    }
}

class Game {
public:
    void Frame(HWND window) {
        for (const auto& message : connection_.Poll()) {
            try {
                Receive(message);
            } catch (const Json::exception&) {
                status_ = "The server sent invalid world data.";
            }
        }
        UpdateInput(window);
        const auto& io = ImGui::GetIO();
        for (auto& [id, actor] : actors_) {
            const float blend = std::min(1.f, io.DeltaTime * 22.f);
            actor.visual.x += (actor.position.x - actor.visual.x) * blend;
            actor.visual.y += (actor.position.y - actor.visual.y) * blend;
        }

        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("ClassiCraft", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(gold), "CLASSICRAFT");
        ImGui::SameLine();
        ImGui::TextDisabled(" /  %s", name_.c_str());
        ImGui::Separator();

        const auto area = ImGui::GetContentRegionAvail();
        const float sidebar = std::min(278.f, area.x * 0.34f);
        ImGui::BeginChild("World", {std::max(1.f, area.x - sidebar - ImGui::GetStyle().ItemSpacing.x), area.y},
            ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawWorld();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Game panels", {sidebar, area.y}, ImGuiChildFlags_Borders);
        DrawPanels();
        ImGui::EndChild();
        ImGui::End();
    }

private:
    void Receive(const Json& message) {
        const auto type = message.value("type", "");
        if (type == "hello") {
            self_ = message.at("id").get<std::string>();
            status_ = "Loading world...";
        } else if (type == "snapshot") {
            const auto& world = message.at("dungeon");
            name_ = world.at("name").get<std::string>();
            objects_ = world.at("objects");
            version_ = message.at("contentVersion").get<std::string>();
            UpdateActors(message.at("actors"));
            auto cached = game::ReadWorldCache(version_);
            if (cached.is_object()) ApplyWorld(cached, "Loaded from disk cache");
            else connection_.Send({{"type", "get_world_content"}, {"version", version_}});
        } else if (type == "world_content" && message.value("version", "") == version_) {
            const auto content = message.at("content").get<std::string>();
            auto world = game::DecodeWorldContent(version_, content);
            if (world.is_object()) {
                ApplyWorld(world, "Downloaded from server");
                game::SaveWorldCache(version_, content);
            } else {
                status_ = "The world download failed its integrity check.";
            }
        } else if (type == "state") {
            UpdateActors(message.at("actors"));
        } else if (type == "item_removed") {
            const auto id = message.at("id").get<std::string>();
            for (auto it = objects_.begin(); it != objects_.end(); ++it) {
                if (it->value("id", "") == id) {
                    objects_.erase(it);
                    break;
                }
            }
        } else if (type == "item_collected") {
            inventory_.push_back(message.at("name").get<std::string>());
            status_ = "Picked up " + inventory_.back() + ".";
        } else if (type == "damage" && message.value("src", "") == self_) {
            status_ = "Smite dealt " + std::to_string(message.at("amt").get<int>()) + " damage.";
        } else if (type == "connection_error") {
            ready_ = false;
            status_ = message.at("message").get<std::string>();
        }
    }

    void ApplyWorld(const Json& world, const char* source) {
        tiles_.clear();
        cells_.clear();
        for (const auto& tile : world.at("tiles")) {
            if (tile.at("pixels").size() == 128)
                tiles_[tile.at("id").get<std::string>()] = tile.at("pixels").get<Tile>();
        }
        for (const auto& cell : world.at("cells")) {
            cells_.push_back({{cell.at("x").get<float>() + .5f, cell.at("y").get<float>() + .5f},
                cell.at("tile").get<std::string>(), cell.at("collision").get<bool>(), cell.value("z", 0)});
        }
        ready_ = true;
        status_ = "Connected.";
        source_ = source;
    }

    void UpdateActors(const Json& state) {
        std::unordered_map<std::string, Actor> next;
        for (const auto& entry : state) {
            auto id = entry.at("id").get<std::string>();
            const Point position{entry.at("x").get<float>(), entry.at("y").get<float>()};
            Actor actor{position, position};
            if (const auto previous = actors_.find(id); previous != actors_.end()) {
                actor = previous->second;
                const float dx = position.x - actor.position.x;
                const float dy = position.y - actor.position.y;
                actor.position = position;
                actor.moving = std::abs(dx) + std::abs(dy) > .001f;
                if (actor.moving) {
                    if (std::abs(dx + dy) > .001f) actor.back = dx + dy < 0;
                    if (std::abs(dx - dy) > .001f) actor.left = dx - dy < 0;
                }
            }
            next.emplace(std::move(id), actor);
        }
        actors_ = std::move(next);
    }

    void UpdateInput(HWND window) {
        if (!ready_) return;
        const bool focused = GetForegroundWindow() == window;
        const auto down = [focused](int key) { return focused && (GetAsyncKeyState(key) & 0x8000) != 0; };
        const int sx = down('D') - down('A');
        const int sy = down('S') - down('W');
        const Point direction{static_cast<float>(sy + sx), static_cast<float>(sy - sx)};
        const double now = ImGui::GetTime();
        if (direction.x != lastInput_.x || direction.y != lastInput_.y ||
            ((sx || sy) && now >= nextInput_)) {
            connection_.Send({{"type", "move_input"}, {"x", direction.x}, {"y", direction.y}});
            lastInput_ = direction;
            nextInput_ = now + .10;
        }
        const bool sprint = down(VK_SHIFT);
        if (sprint != sprint_) {
            sprint_ = sprint;
            connection_.Send({{"type", "mount"}, {"mounted", sprint}});
        }
        if (focused && ImGui::IsKeyPressed(ImGuiKey_E, false)) PickUp();
        if (focused && ImGui::IsKeyPressed(ImGuiKey_Space, false) && !target_.empty())
            connection_.Send({{"type", "cast"}, {"ability", "smite"}, {"target", target_}});
    }

    static Point ObjectPosition(const Json& object) {
        return {object.at("x").get<float>() + .5f, object.at("y").get<float>() + .5f};
    }

    void PickUp() {
        const auto player = actors_.find(self_);
        if (player == actors_.end()) return;
        float best = 1.5f;
        std::string item;
        for (const auto& object : objects_) {
            if (object.value("kind", "") != "item" || object.value("z", 0) != 0) continue;
            const auto position = ObjectPosition(object);
            const auto distance = std::hypot(position.x - player->second.position.x, position.y - player->second.position.y);
            if (distance <= best) {
                best = distance;
                item = object.at("id").get<std::string>();
            }
        }
        if (item.empty()) status_ = "Move closer to an item.";
        else connection_.Send({{"type", "pickup"}, {"id", item}});
    }

    void DrawTile(ImDrawList& draw, ImVec2 center, const std::string& id, float scale, bool shade, float verticalScale = 0.f) const {
        if (verticalScale == 0.f) verticalScale = scale;
        const auto tile = tiles_.find(id);
        if (tile == tiles_.end()) return;
        const ImVec2 start{std::floor(center.x - 8 * scale), std::floor(center.y - 4 * verticalScale)};
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 16;) {
                auto color = tile->second[y * 16 + x];
                int end = x + 1;
                while (end < 16 && tile->second[y * 16 + end] == color) ++end;
                if (color >> 24) {
                    if (shade) color = (color & 0xff000000) | ((color & 0x00fefefe) >> 1);
                    draw.AddRectFilled({start.x + x * scale, start.y + y * verticalScale},
                        {start.x + end * scale, start.y + (y + 1) * verticalScale}, color);
                }
                x = end;
            }
        }
    }

    void DrawRaisedTile(ImDrawList& draw, ImVec2 feet, const Cell& cell) const {
        const float height = cell.z * layerHeight;
        const ImVec2 top{feet.x, feet.y - height};
        if (cell.blocked) {
            const ImVec2 left{feet.x - tileWidth / 2, feet.y};
            const ImVec2 right{feet.x + tileWidth / 2, feet.y};
            const ImVec2 front{feet.x, feet.y + tileHeight / 2};
            draw.AddQuadFilled({left.x, left.y - height}, {front.x, front.y - height},
                front, left, IM_COL32(62, 53, 66, 255));
            draw.AddQuadFilled({front.x, front.y - height}, {right.x, right.y - height},
                right, front, IM_COL32(45, 40, 52, 255));
            for (float seam = layerHeight; seam < height; seam += layerHeight) {
                draw.AddLine({left.x, left.y - seam}, {front.x, front.y - seam}, IM_COL32(28, 25, 33, 130));
                draw.AddLine({front.x, front.y - seam}, {right.x, right.y - seam}, IM_COL32(22, 20, 28, 155));
            }
        }
        DrawTile(draw, top, cell.tile, 3.f, false);
    }

    void DrawWorld() {
        auto* draw = ImGui::GetWindowDrawList();
        const auto top = ImGui::GetCursorScreenPos();
        const auto size = ImGui::GetContentRegionAvail();
        const ImVec2 bottom{top.x + size.x, top.y + size.y};
        draw->AddRectFilled(top, bottom, IM_COL32(26, 31, 40, 255), 4);
        if (cells_.empty()) return;

        const auto player = actors_.find(self_);
        const auto camera = player == actors_.end() ? Point{} : player->second.visual;
        const ImVec2 center{top.x + size.x * .5f, top.y + size.y * .5f};
        const auto project = [camera, center](Point point) {
            const float x = point.x - camera.x, y = point.y - camera.y;
            return ImVec2{center.x + (x - y) * tileWidth / 2, center.y + (x + y) * tileHeight / 2};
        };
        const auto visible = [top, bottom](ImVec2 point) {
            return point.x >= top.x - 65 && point.x <= bottom.x + 65 &&
                point.y >= top.y - 60 && point.y <= bottom.y + 60;
        };

        draw->PushClipRect(top, bottom, true);
        for (const auto& cell : cells_) {
            const auto point = project(cell.center);
            if (cell.z == 0 && visible(point)) DrawTile(*draw, point, cell.tile, 3.f, cell.blocked);
        }

        struct Entity {
            ImVec2 feet;
            const Json* object{};
            const Actor* actor{};
            bool remote{};
            const Cell* layer{};
            float depth{};
        };
        std::vector<Entity> visibleEntities;
        for (const auto& cell : cells_) {
            const auto point = project(cell.center);
            if (cell.z > 0 && visible({point.x, point.y - cell.z * layerHeight / 2}))
                visibleEntities.push_back({point, nullptr, nullptr, false, &cell,
                    point.y + (cell.blocked ? tileHeight / 2 : 0.f)});
        }
        for (const auto& object : objects_) {
            const auto ground = project(ObjectPosition(object));
            const ImVec2 point{ground.x, ground.y - object.value("z", 0) * layerHeight};
            if (visible(point)) visibleEntities.push_back({point, &object, nullptr, false, nullptr, ground.y});
        }
        for (const auto& [id, actor] : actors_) {
            const auto point = project(actor.visual);
            if (visible(point)) visibleEntities.push_back({point, nullptr, &actor, id != self_, nullptr, point.y});
        }
        std::ranges::stable_sort(visibleEntities, [](const Entity& first, const Entity& second) {
            if (first.depth != second.depth) return first.depth < second.depth;
            const int firstLayer = first.layer ? first.layer->z : 8;
            const int secondLayer = second.layer ? second.layer->z : 8;
            return firstLayer < secondLayer;
        });
        const auto mouse = ImGui::GetMousePos();
        const bool selecting = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (selecting) {
            target_.clear();
            targetName_.clear();
        }
        for (const auto& entity : visibleEntities) {
            const auto feet = entity.feet;
            if (entity.layer) {
                DrawRaisedTile(*draw, feet, *entity.layer);
                continue;
            }
            if (entity.actor) {
                DrawCharacter(*draw, feet, false, *entity.actor, entity.remote);
                continue;
            }
            const auto& object = *entity.object;
            const auto kind = object.value("kind", "");
            const auto art = object.value("artwork", "");
            if (kind == "spawn") {
                draw->AddEllipse(feet, {12, 5}, IM_COL32(160, 193, 171, 255));
                continue;
            }
            if (!art.empty()) {
                const bool tree = art == "oak-tree" || art == "pine-tree";
                const bool castsShadow = kind == "object" || kind == "item";
                if (castsShadow) {
                    draw->AddEllipseFilled({feet.x + 6, feet.y + 2}, {tree ? 22.f : 16.f, 7}, IM_COL32(14, 21, 25, 75));
                    draw->AddEllipseFilled({feet.x + 1, feet.y}, {tree ? 13.f : 10.f, 4}, IM_COL32(14, 21, 25, 90));
                }
                const float artHeight = tree ? 5.f : 3.f;
                DrawTile(*draw, {feet.x, feet.y - 4 * artHeight + 3}, art, 3.f, false, artHeight);
            }
            else if (kind == "enemy") DrawCharacter(*draw, feet, true, Actor{}, false);
            else if (kind == "object") draw->AddRectFilled({feet.x - 11, feet.y - 22}, {feet.x + 11, feet.y}, IM_COL32(94, 102, 116, 255), 3);
            if (kind == "enemy" && object.value("z", 0) == 0 && selecting && std::abs(mouse.x - feet.x) < 22 &&
                mouse.y > feet.y - 46 && mouse.y < feet.y + 8) {
                target_ = object.value("id", "");
                targetName_ = object.value("name", "");
            }
            if (kind == "enemy" && target_ == object.value("id", ""))
                draw->AddEllipse(feet, {17, 7}, gold, 0.f, 0, 2.f);
            if (kind == "enemy" || kind == "item") {
                const auto name = object.value("name", "");
                draw->AddText({feet.x + 17, feet.y - 30}, IM_COL32(240, 224, 191, 255), name.c_str());
            }
        }
        draw->PopClipRect();
    }

    void DrawPanels() const {
        ImGui::SeparatorText("World");
        ImGui::TextWrapped("%s", status_.c_str());
        if (!source_.empty()) ImGui::TextDisabled("%s", source_.c_str());
        if (const auto actor = actors_.find(self_); actor != actors_.end())
            ImGui::TextDisabled("Tile %d, %d", static_cast<int>(actor->second.position.x),
                static_cast<int>(actor->second.position.y));

        ImGui::Spacing();
        ImGui::SeparatorText("Inventory");
        if (inventory_.empty()) ImGui::TextDisabled("Empty");
        for (const auto& item : inventory_) ImGui::BulletText("%s", item.c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Target");
        if (targetName_.empty()) ImGui::TextDisabled("Right-click the training dummy");
        else ImGui::TextUnformatted(targetName_.c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Controls");
        ImGui::TextUnformatted("WASD   Move\nShift      Sprint\nE          Pick up\nSpace   Smite");
    }

    game::Connection connection_;
    std::string self_, version_, source_, target_, targetName_;
    std::string name_{"Test world"}, status_{"Connecting..."};
    Json objects_ = Json::array();
    std::unordered_map<std::string, Tile> tiles_;
    std::vector<Cell> cells_;
    std::unordered_map<std::string, Actor> actors_;
    std::vector<std::string> inventory_;
    Point lastInput_{};
    double nextInput_{};
    bool sprint_{}, ready_{};
};

std::unique_ptr<Game> gameWindow;
}

void DrawGame(HWND window) {
    if (!gameWindow) gameWindow = std::make_unique<Game>();
    gameWindow->Frame(window);
}

void ShutdownGame() {
    gameWindow.reset();
}
