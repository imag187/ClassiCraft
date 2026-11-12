#include "imgui.h"
#include "imgui_stdlib.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <unordered_map>

using Json = nlohmann::json;
namespace fs = std::filesystem;
void DrawWorldEditor(const fs::path& directory);
namespace {
constexpr std::array kinds{"classes", "abilities", "trees", "progression", "equipmentSlots", "itemCategories"};
constexpr int equipmentKind = 4;
constexpr int itemCategoryKind = 5;
constexpr int libraryVersion = 3;
constexpr std::array labels{"Classes", "Spells & abilities", "Talent trees", "Progression", "Equipment slots", "Item categories"};
constexpr std::array singular{"Class", "Ability", "Talent tree", "Progression profile", "Equipment slot", "Item category"};
Json document;
std::string draft, status;
int category = 0, selected = 0, level = 1, profile = 0;
bool loaded = false;
std::unordered_set<std::string> spent;

Json NewRecord(int kind, const std::string& id) {
    Json row{{"id", id}, {"name", std::string("New ") + singular[kind]}, {"description", ""}};
    switch (kind) {
    case 0: row.update(Json{{"icon", 0}, {"resource", "Stamina"}, {"allowedItemCategories", Json::array()}, {"tree", ""}, {"unlocks", Json::array()}}); break;
    case 1: row.update(Json{{"icon", 1}, {"aim", "Facing"}, {"fallback", "Require focus"}, {"shape", "Arc"}, {"activation", "Press"}, {"castMs", 0}, {"moving", true}, {"cost", 0}, {"damage", 0}, {"weaponPercent", 100}, {"ranks", Json::array()}, {"chargeTiers", Json::array()}}); break;
    case 2: row["nodes"] = Json::array(); break;
    case 3: row.update(Json{{"cap", 20}, {"firstTalentLevel", 5}, {"levelsPerPoint", 1}, {"baseXp", 100}, {"xpPerLevel", 50}}); break;
    case itemCategoryKind: row["group"] = "Armor"; break;
    case equipmentKind: row.update(Json{{"count", 1}, {"itemCategories", Json::array()}}); break;
    }
    return row;
}

int Find(int kind, const std::string& id) {
    const auto& records = document.at(kinds[kind]);
    for (int i = 0; i < static_cast<int>(records.size()); ++i)
        if (records[i].at("id") == id) return i;
    return -1;
}

void Select(int kind, int index) {
    category = kind;
    selected = index;
    spent.clear();
    draft = index < 0 ? "" : document[kinds[kind]][index].dump(2);
}

void ValidateRecord(int kind, const Json& row) {
    const auto expected = NewRecord(kind, "");
    for (auto it = expected.begin(); it != expected.end(); ++it) {
        if (!row.contains(it.key()) ||
            (it->is_number_integer() ? !row.at(it.key()).is_number_integer() : row.at(it.key()).type() != it->type()))
            throw std::runtime_error("Missing or invalid field: " + it.key());
    }
    if (row.at("id") == "" || row.at("name") == "") throw std::runtime_error("Name and ID are required.");
    auto require = [](const Json& list, const char* field, bool number) {
        for (const auto& item : list)
            if (!item.contains(field) || (number ? !item[field].is_number_integer() : !item[field].is_string()))
                throw std::runtime_error(std::string("Missing or invalid field: ") + field);
    };
    if (kind == 0 || kind == equipmentKind) {
        const auto& categories = row.at(kind == 0 ? "allowedItemCategories" : "itemCategories");
        std::unordered_set<std::string> unique;
        for (const auto& entry : categories) {
            if (!entry.is_string() || entry.get_ref<const std::string&>().find_first_not_of(" \t\r\n") == std::string::npos ||
                !unique.insert(entry.get<std::string>()).second)
                throw std::runtime_error("Item categories must be nonempty, unique text values.");
        }
    }
    if (kind == equipmentKind && (row["count"] < 1 || row["count"] > 20))
        throw std::runtime_error("An equipment slot must have between 1 and 20 positions.");
    if (kind == 0) {
        require(row["unlocks"], "ability", false);
        require(row["unlocks"], "level", true);
    }
    if (kind == 1) {
        require(row["ranks"], "level", true);
        require(row["ranks"], "damage", true);
        require(row["chargeTiers"], "holdMs", true);
        require(row["chargeTiers"], "damagePercent", true);
    }
    if (kind == 2) {
        require(row["nodes"], "id", false);
        require(row["nodes"], "name", false);
        require(row["nodes"], "requires", false);
        require(row["nodes"], "ability", false);
        require(row["nodes"], "cost", true);
        require(row["nodes"], "moving", true);
        std::unordered_set<std::string> ids;
        for (const auto& node : row["nodes"])
            if (node["id"] == "" || !ids.insert(node["id"].get<std::string>()).second || node["cost"] < 1)
                throw std::runtime_error("Talent IDs must be unique and costs must be positive.");
        for (const auto& node : row["nodes"]) {
            auto prerequisite = node["requires"].get<std::string>();
            std::unordered_set<std::string> chain{node["id"].get<std::string>()};
            while (!prerequisite.empty()) {
                auto parent = std::find_if(row["nodes"].begin(), row["nodes"].end(),
                    [&](const Json& candidate) { return candidate["id"] == prerequisite; });
                if (parent == row["nodes"].end() || !chain.insert(prerequisite).second)
                    throw std::runtime_error("Talent prerequisites must exist and cannot form a cycle.");
                prerequisite = (*parent)["requires"].get<std::string>();
            }
        }
    }
    if (kind == 3 && (row["cap"] < 1 || row["cap"] > 1000 || row["levelsPerPoint"] < 1 || row["firstTalentLevel"] < 1 || row["baseXp"] < 0 || row["xpPerLevel"] < 0))
        throw std::runtime_error("Use a cap from 1 to 1000, positive talent intervals, and nonnegative XP.");
}

void UpgradeLegacySlots(Json& library) {
    if (library.at("version") != 1) return;

    Json slots = Json::array();
    std::unordered_set<std::string> names;
    for (auto& klass : library.at("classes")) {
        const auto& legacy = klass.at("equipment");
        if (!legacy.is_array()) throw std::runtime_error("Expected a legacy equipment list.");
        for (const auto& value : legacy) {
            const auto name = value.get<std::string>();
            if (name.find_first_not_of(" \t\r\n") == std::string::npos || !names.insert(name).second) continue;
            auto slot = NewRecord(equipmentKind, "slot-" + std::to_string(slots.size() + 1));
            slot["name"] = name;
            slots.push_back(std::move(slot));
        }
        klass.erase("equipment");
        klass["allowedItemCategories"] = Json::array();
    }
    library["equipmentSlots"] = std::move(slots);
    library["version"] = 2;
}

void UpgradeLibrary(Json& library) {
    UpgradeLegacySlots(library);
    if (library.at("version") != 2) return;

    Json definitions = Json::array();
    std::unordered_map<std::string, std::string> ids;
    auto migrate = [&](Json& categories) {
        if (!categories.is_array()) throw std::runtime_error("Expected an item category list.");
        for (auto& category : categories) {
            const auto name = category.get<std::string>();
            auto [entry, inserted] = ids.try_emplace(name, "category-" + std::to_string(ids.size() + 1));
            if (inserted) {
                auto definition = NewRecord(itemCategoryKind, entry->second);
                definition["name"] = name;
                definition["group"] = "";
                definitions.push_back(std::move(definition));
            }
            category = entry->second;
        }
    };
    for (auto& slot : library.at("equipmentSlots")) migrate(slot.at("itemCategories"));
    for (auto& klass : library.at("classes")) migrate(klass.at("allowedItemCategories"));
    library["itemCategories"] = std::move(definitions);
    library["version"] = libraryVersion;
}

void Open(const fs::path& path) {
    try {
        std::ifstream input(path);
        Json next = Json::parse(input);
        const bool upgraded = next.at("version") != libraryVersion;
        UpgradeLibrary(next);
        if (next.at("version") != libraryVersion) throw std::runtime_error("Unsupported library version.");
        for (int k = 0; k < static_cast<int>(kinds.size()); ++k) {
            if (!next.at(kinds[k]).is_array()) throw std::runtime_error("Expected a list of records.");
            std::unordered_set<std::string> ids;
            for (const auto& record : next[kinds[k]]) {
                ValidateRecord(k, record);
                if (!ids.insert(record.at("id").get<std::string>()).second)
                    throw std::runtime_error("Duplicate record ID.");
            }
        }
        document = std::move(next);
        profile = 0;
        Select(0, document["classes"].empty() ? -1 : 0);
        status = upgraded ? "Equipment definitions upgraded. Save to keep the upgrade." : "Library opened";
    } catch (const std::exception& e) { status = e.what(); }
}

void Icon(ImTextureID atlas, int index, float size) {
    if (!atlas || index < 0 || index > 11) return;
    const float x = (index % 4) / 4.0f, y = (index / 4) / 3.0f;
    ImGui::Image(atlas, {size, size}, {x, y}, {x + .25f, y + 1.0f / 3});
    ImGui::SameLine();
}

void Link(int kind, const std::string& id) {
    const int index = Find(kind, id);
    if (index < 0) {
        ImGui::TextDisabled("%s", id.empty() ? "None assigned" : ("Missing reference: " + id).c_str());
        return;
    }
    const auto title = document[kinds[kind]][index]["name"].get<std::string>() + "##" + id;
    if (ImGui::SmallButton(title.c_str())) Select(kind, index);
}

const char* FieldLabel(const std::string& key) {
    static const std::unordered_map<std::string, const char*> labels{
        {"name","Name"}, {"description","Description"}, {"resource","Class resource"},
        {"tree","Talent tree"}, {"allowedItemCategories","Equipment proficiency"},
        {"itemCategories","Accepted item types"}, {"count","Number of positions"},
        {"group","Group"}, {"unlocks","Abilities learned by level"}, {"ability","Ability"},
        {"aim","Aim at"}, {"fallback","When no target is focused"}, {"shape","Hit area"},
        {"activation","How it activates"}, {"castMs","Cast time (ms)"}, {"moving","Cast while moving"},
        {"cost","Cost"}, {"damage","Base damage"}, {"weaponPercent","Weapon damage (%)"},
        {"ranks","Spell ranks"}, {"chargeTiers","Charge tiers"}, {"level","Level required"},
        {"holdMs","Hold time (ms)"}, {"damagePercent","Damage multiplier (%)"}, {"nodes","Talents"},
        {"requires","Prerequisite"}, {"cap","Level cap"}, {"firstTalentLevel","First talent point at level"},
        {"levelsPerPoint","Levels per talent point"}, {"baseXp","XP from level 1 to 2"},
        {"xpPerLevel","Extra XP required per level"}
    };
    auto it = labels.find(key);
    return it == labels.end() ? key.c_str() : it->second;
}

void TextChoice(const char* label, std::string& value, std::initializer_list<const char*> choices) {
    if (!ImGui::BeginCombo(label, value.c_str())) return;
    bool known = false;
    for (const auto* choice : choices) {
        known = known || value == choice;
        if (ImGui::Selectable(choice, value == choice)) value = choice;
    }
    if (!known) ImGui::Selectable(value.c_str(), true);
    ImGui::EndCombo();
}

void TalentPrerequisite(std::string& value, const Json& node, const Json& nodes) {
    const char* current = value.empty() ? "None" : value.c_str();
    for (const auto& candidate : nodes)
        if (candidate.at("id") == value) current = candidate.at("name").get_ref<const std::string&>().c_str();
    if (!ImGui::BeginCombo("Prerequisite", current)) return;
    if (ImGui::Selectable("None", value.empty())) value.clear();
    for (const auto& candidate : nodes) {
        const auto id = candidate.at("id").get<std::string>();
        if (node.at("id") == id) continue;
        const auto name = candidate.at("name").get<std::string>() + "##" + id;
        if (ImGui::Selectable(name.c_str(), value == id)) value = id;
    }
    ImGui::EndCombo();
}

void EditFields(Json& record, ImTextureID atlas, const Json* nodes = nullptr);
void Reference(const char* label, std::string& id, int kind, bool allowNone = true);
void EditList(const std::string& key, Json& list, ImTextureID atlas) {
    const bool categories = key == "allowedItemCategories" || key == "itemCategories";
    const char* add = categories ? "Add item type" : key == "unlocks" ? "Add ability unlock" :
        key == "ranks" ? "Add rank" : key == "chargeTiers" ? "Add charge tier" : "Add talent";
    if (categories && list.empty())
        ImGui::TextDisabled("%s", key == "allowedItemCategories" ? "Use all types accepted by each slot." : "Accept any item type.");
    if (key == "ranks" && list.empty()) ImGui::TextDisabled("This ability has no ranks.");
    if (key == "chargeTiers" && list.empty()) ImGui::TextDisabled("This ability has no charge tiers.");
    int remove = -1;
    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        ImGui::PushID(i);
        if (list[i].is_object()) {
            const auto heading = list[i].value("name", std::string(add + 4)) + "##entry";
            if (ImGui::TreeNodeEx(heading.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                EditFields(list[i], atlas, key == "nodes" ? &list : nullptr);
                if (ImGui::SmallButton("Remove")) remove = i;
                ImGui::TreePop();
            }
        } else if (list[i].is_string()) {
            auto& value = list[i].get_ref<std::string&>();
            ImGui::SetNextItemWidth(-85);
            if (categories) Reference("##Type", value, itemCategoryKind, false);
            else ImGui::InputText("##Value", &value);
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) remove = i;
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        if (key == "nodes") {
            const auto id = list[remove].at("id");
            for (auto& node : list)
                if (node.at("requires") == id) node["requires"] = "";
        }
        list.erase(remove);
    }
    if (!ImGui::SmallButton(add)) return;
    if (categories) list.push_back("");
    if (key == "unlocks") list.push_back(Json{{"level", 1}, {"ability", ""}});
    if (key == "ranks") list.push_back(Json{{"level", 1}, {"damage", 0}});
    if (key == "chargeTiers") list.push_back(Json{{"holdMs", 500}, {"damagePercent", 150}});
    if (key == "nodes") {
        int number = 1;
        std::string id;
        do { id = "talent-" + std::to_string(number++); }
        while (std::any_of(list.begin(), list.end(), [&](const Json& node) { return node.at("id") == id; }));
        list.push_back(Json{{"id", id}, {"name", "New talent"}, {"cost", 1}, {"requires", ""}, {"ability", ""}, {"moving", -1}});
    }
}

void Reference(const char* label, std::string& id, int kind, bool allowNone) {
    const int index = Find(kind, id);
    const std::string current = index < 0 ? (id.empty() ? "None" : id) : document[kinds[kind]][index]["name"].get<std::string>();
    if (!ImGui::BeginCombo(label, current.c_str())) return;
    if (allowNone && ImGui::Selectable("None", id.empty())) id.clear();
    for (const auto& entry : document[kinds[kind]]) {
        const auto choice = entry["id"].get<std::string>();
        const auto group = entry.value("group", std::string{});
        const auto name = entry["name"].get<std::string>() + (group.empty() ? "" : " (" + group + ")") + "##" + choice;
        if (ImGui::Selectable(name.c_str(), id == choice)) id = choice;
    }
    ImGui::EndCombo();
}

void EditFields(Json& record, ImTextureID atlas, const Json* nodes) {
    constexpr std::array order{"name", "description", "icon", "group", "resource", "tree",
        "unlocks", "allowedItemCategories", "count", "itemCategories", "activation", "aim", "fallback",
        "shape", "castMs", "moving", "cost", "damage", "weaponPercent", "ranks", "chargeTiers",
        "nodes", "ability", "requires", "level", "holdMs", "damagePercent", "cap", "baseXp",
        "xpPerLevel", "firstTalentLevel", "levelsPerPoint"};
    for (const std::string key : order) {
        if (!record.contains(key)) continue;
        auto& value = record[key];
        const char* label = FieldLabel(key);
        ImGui::PushID(key.c_str());
        if (key != "icon" && !value.is_array()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
        }
        if (key == "icon" && atlas) {
            if (ImGui::TreeNode("Icon")) {
                for (int i = 0; i < 12; ++i) {
                    ImGui::PushID(i);
                    const float x = (i % 4) / 4.f, y = (i / 4) / 3.f;
                    if (ImGui::ImageButton("Pick", atlas, {28, 28}, {x, y}, {x + .25f, y + 1.f / 3},
                        value == i ? ImVec4(.8f, .55f, .2f, 1) : ImVec4(0, 0, 0, 0))) value = i;
                    ImGui::PopID();
                    if (i % 6 != 5) ImGui::SameLine();
                }
                ImGui::TreePop();
            }
        } else if (value.is_array()) {
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) EditList(key, value, atlas);
        } else if (value.is_string()) {
            auto& str = value.get_ref<std::string&>();
            if (key == "ability" || key == "tree") Reference("##Value", str, key == "ability" ? 1 : 2);
            else if (key == "requires" && nodes) TalentPrerequisite(str, record, *nodes);
            else if (key == "activation") TextChoice("##Value", str, {"Press", "Hold and release", "Auto attack"});
            else if (key == "aim") TextChoice("##Value", str, {"Facing", "Focused enemy", "Ground point", "Self"});
            else if (key == "shape") TextChoice("##Value", str, {"Single", "Arc", "Cone", "Line", "Radius"});
            else if (key == "fallback") TextChoice("##Value", str, {"Require focus", "Closest eligible target"});
            else if (key == "description") ImGui::InputTextMultiline("##Value", &str, {-1, 58});
            else ImGui::InputText("##Value", &str);
        } else if (value.is_boolean()) {
            ImGui::Checkbox("##Value", &value.get_ref<bool&>());
        } else if (value.is_number_integer()) {
            int n = value.get<int>();
            if (key == "moving") {
                const char* options[]{"No change", "Require standing still", "Allow movement"};
                int choice = std::clamp(n + 1, 0, 2);
                if (ImGui::Combo("##Value", &choice, options, 3)) value = choice - 1;
            } else if (ImGui::InputInt("##Value", &n)) value = n;
        }
        ImGui::PopID();
    }
}

std::string CategoryList(const Json& categories) {
    std::string result;
    for (const auto& category : categories) {
        if (!result.empty()) result += ", ";
        const auto id = category.get<std::string>();
        const int index = Find(itemCategoryKind, id);
        result += index < 0 ? "Missing: " + id : document[kinds[itemCategoryKind]][index].at("name").get<std::string>();
    }
    return result;
}

void PreviewEquipment(const Json& klass) {
    ImGui::SeparatorText("Shared equipment slots");
    const auto& slots = document.at("equipmentSlots");
    const auto& allowed = klass.at("allowedItemCategories");
    if (slots.empty()) ImGui::TextDisabled("Add slots in Equipment slots.");
    for (const auto& slot : slots) {
        const auto& accepted = slot.at("itemCategories");
        Json usable = Json::array();
        if (accepted.empty()) usable = allowed;
        else {
            for (const auto& category : accepted)
                if (allowed.empty() || std::find(allowed.begin(), allowed.end(), category) != allowed.end())
                    usable.push_back(category);
        }
        ImGui::PushID(slot.at("id").get_ref<const std::string&>().c_str());
        Link(equipmentKind, slot.at("id"));
        ImGui::SameLine();
        ImGui::TextDisabled("x%d", slot.at("count").get<int>());
        if (!usable.empty()) ImGui::TextWrapped("%s", CategoryList(usable).c_str());
        else if (accepted.empty() && allowed.empty()) ImGui::TextDisabled("Any item category");
        else ImGui::TextDisabled("No eligible item categories");
        ImGui::PopID();
    }
}

void Preview(const Json& row, ImTextureID atlas) {
    ImGui::SeparatorText("Preview");
    const int kind = category;
    if (kind == 0 || kind == 1 || kind == 2) {
        auto& profiles = document["progression"];
        profile = std::clamp(profile, 0, std::max(0, static_cast<int>(profiles.size()) - 1));
        const Json rules = profiles.empty() ? NewRecord(3, "preview") : profiles[profile];
        if (ImGui::BeginCombo("Progression", profiles.empty() ? "Temporary preview" : rules["name"].get_ref<const std::string&>().c_str())) {
            for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(profiles[i]["name"].get_ref<const std::string&>().c_str(), i == profile)) { profile = i; spent.clear(); }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        const int cap = rules["cap"];
        level = std::clamp(level, 1, cap);
        if (ImGui::SliderInt("Level", &level, 1, cap)) spent.clear();
        const int first = rules["firstTalentLevel"], interval = rules["levelsPerPoint"];
        const int points = level < first ? 0 : 1 + (level - first) / interval;
        ImGui::Text("Talent points earned: %d", points);

        if (kind == 0) {
            ImGui::SeparatorText("Spellbook & unlocks");
            for (const auto& unlock : row["unlocks"]) {
                ImGui::Text("Level %d%s", unlock["level"].get<int>(), unlock["level"] > level ? "  (locked)" : "");
                ImGui::SameLine();
                Link(1, unlock["ability"]);
            }
            ImGui::SeparatorText("Talent tree");
            Link(2, row["tree"]);
            PreviewEquipment(row);
        }
        if (kind == 1) {
            Icon(atlas, row["icon"], 44);
            ImGui::TextUnformatted(row["name"].get_ref<const std::string&>().c_str());
            int damage = row["damage"], best = 0;
            for (const auto& rank : row["ranks"]) {
                if (rank["level"] <= level && rank["level"] >= best) {
                    best = rank["level"];
                    damage = rank["damage"];
                }
            }
            ImGui::Text("%d damage + %d%% weapon damage", damage, row["weaponPercent"].get<int>());
            ImGui::Text("%s / %s / %s", row["activation"].get_ref<const std::string&>().c_str(),
                row["aim"].get_ref<const std::string&>().c_str(), row["shape"].get_ref<const std::string&>().c_str());
            ImGui::Text("%d ms cast | %d resource | %s", row["castMs"].get<int>(), row["cost"].get<int>(),
                row["moving"].get<bool>() ? "Mobile" : "Stationary");
            for (const auto& tier : row["chargeTiers"])
                ImGui::Text("Hold %d ms: %d%% damage", tier["holdMs"].get<int>(), tier["damagePercent"].get<int>());
            ImGui::SeparatorText("Used by");
            for (const auto& klass : document["classes"])
                if (std::any_of(klass["unlocks"].begin(), klass["unlocks"].end(),
                    [&](const Json& unlock) { return unlock["ability"] == row["id"]; })) Link(0, klass["id"]);
            for (const auto& tree : document["trees"])
                if (std::any_of(tree["nodes"].begin(), tree["nodes"].end(),
                    [&](const Json& node) { return node["ability"] == row["id"]; })) Link(2, tree["id"]);
        }
        if (kind == 2) {
            int available = points;
            for (const auto& node : row["nodes"])
                if (spent.contains(node["id"].get<std::string>())) available -= node["cost"].get<int>();
            ImGui::Text("Unspent: %d", available);
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset points")) spent.clear();
            for (const auto& node : row["nodes"]) {
                const std::string id = node["id"], requiresId = node["requires"];
                ImGui::PushID(id.c_str());
                ImGui::SeparatorText(node["name"].get_ref<const std::string&>().c_str());
                if (!requiresId.empty()) ImGui::Text("Requires %s", requiresId.c_str());
                Link(1, node["ability"]);
                if (node["moving"] >= 0) ImGui::TextUnformatted(node["moving"] == 1 ? "Allows casting while moving" : "Requires stationary casting");
                const bool bought = spent.contains(id);
                ImGui::BeginDisabled(bought || available < node["cost"] || (!requiresId.empty() && !spent.contains(requiresId)));
                if (ImGui::SmallButton(bought ? "Learned" : ("Spend " + node["cost"].dump()).c_str())) spent.insert(id);
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
    } else if (kind == itemCategoryKind) {
        const auto& group = row.at("group").get_ref<const std::string&>();
        if (!group.empty()) ImGui::TextUnformatted(group.c_str());
        ImGui::SeparatorText("Explicitly referenced by");
        for (const int source : {equipmentKind, 0}) {
            for (const auto& record : document.at(kinds[source])) {
                const auto& categories = record.at(source == 0 ? "allowedItemCategories" : "itemCategories");
                if (std::find(categories.begin(), categories.end(), row.at("id")) != categories.end())
                    Link(source, record.at("id"));
            }
        }
    } else if (kind == equipmentKind) {
        ImGui::Text("%d equipment position(s)", row.at("count").get<int>());
        const auto& categories = row.at("itemCategories");
        ImGui::TextWrapped("%s", categories.empty() ? "Any item category" : CategoryList(categories).c_str());
        ImGui::SeparatorText("Available to all classes");
        for (const auto& klass : document.at("classes")) Link(0, klass.at("id"));
    } else {
        int cap = row["cap"];
        level = std::clamp(level, 1, cap);
        ImGui::SliderInt("Level", &level, 1, cap);
        const long long xp = row["baseXp"].get<int>() + (level - 1LL) * row["xpPerLevel"].get<int>();
        if (level < cap) ImGui::Text("XP to next level: %lld", xp);
        else ImGui::TextUnformatted("Level cap reached");
        const int first = row["firstTalentLevel"];
        ImGui::Text("Talent points earned: %d", level < first ? 0 : 1 + (level - first) / row["levelsPerPoint"].get<int>());
    }
}

void SaveLibrary(const fs::path& library) {
    try {
        auto temporary = library;
        temporary += ".tmp";
        std::ofstream file(temporary);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file << document.dump(2) << '\n';
        file.close();
        fs::rename(temporary, library);
        status = "Saved library.json";
    } catch (const std::exception& e) { status = e.what(); }
}
}

void DrawEditor(const fs::path& directory, ImTextureID atlas) {
    const auto library = directory / "library.json";
    if (!loaded) {
        loaded = true;
        document = {{"version", libraryVersion}};
        for (const auto kind : kinds) document[kind] = Json::array();
        Open(fs::exists(library) ? library : directory / "example.json");
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::Begin("ClassiCraft Studio", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored({0.95f, .76f, .44f, 1}, "CLASSICRAFT STUDIO");
    static bool worldMode = false;
    ImGui::SameLine();
    if (ImGui::Button(worldMode ? "Content library" : "World tools")) worldMode = !worldMode;
    if (worldMode) {
        DrawWorldEditor(directory);
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save library")) {
        SaveLibrary(library);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload saved")) Open(library);
    ImGui::SameLine();
    ImGui::TextUnformatted(status.c_str());

    static ImGuiTextFilter search;
    ImGui::BeginChild("Library", {220, 0}, ImGuiChildFlags_Borders);
    ImGui::SeparatorText("Content library");
    for (int k = 0; k < static_cast<int>(kinds.size()); ++k)
        if (ImGui::Selectable(labels[k], category == k)) {
            search.Clear();
            Select(k, document[kinds[k]].empty() ? -1 : 0);
        }
    ImGui::Separator();
    auto& records = document[kinds[category]];
    if (ImGui::Button((std::string("New ") + singular[category]).c_str())) {
        int n = 1;
        std::string id;
        do { id = "new-" + std::to_string(n++); } while (Find(category, id) >= 0);
        records.push_back(NewRecord(category, id));
        Select(category, static_cast<int>(records.size()) - 1);
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##Search", "Filter by name", search.InputBuf, sizeof(search.InputBuf))) search.Build();
    for (int i = 0; i < static_cast<int>(records.size()); ++i) {
        if (!search.PassFilter(records[i]["name"].get_ref<const std::string&>().c_str())) continue;
        ImGui::PushID(i);
        Icon(atlas, records[i].value("icon", -1), 20);
        if (ImGui::Selectable(records[i]["name"].get_ref<const std::string&>().c_str(), i == selected)) Select(category, i);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (selected >= 0) {
        const Json record = records[selected];
        ImGui::BeginChild("Definition", {std::max(260.0f, ImGui::GetContentRegionAvail().x * .57f), 0}, ImGuiChildFlags_Borders);
        Icon(atlas, record.value("icon", -1), 32);
        ImGui::TextUnformatted(record["name"].get_ref<const std::string&>().c_str());
        static Json form;
        static std::string source;
        if (source != record.dump()) {
            form = record;
            source = record.dump();
        }
        bool apply = false, save = false;
        if (ImGui::BeginTabBar("Editor modes")) {
            if (ImGui::BeginTabItem("Design")) {
                if (category == 0) ImGui::TextWrapped("Every class uses the shared slots. Equipment proficiency can narrow what this class can equip.");
                if (category == equipmentKind) ImGui::TextWrapped("This slot appears on every class. Set the number of positions to 2 for a pair of rings.");
                if (ImGui::Button("Save changes")) {
                    draft = form.dump(2);
                    apply = save = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Update preview")) {
                    draft = form.dump(2);
                    apply = true;
                }
                ImGui::Separator();
                EditFields(form, atlas);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced JSON")) {
                ImGui::InputTextMultiline("##definition", &draft, {-1, 350});
                if (ImGui::Button("Apply JSON")) apply = true;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (apply) {
            try {
                auto updated = Json::parse(draft);
                ValidateRecord(category, updated);
                if (category == 0 || category == equipmentKind) {
                    for (const auto& id : updated.at(category == 0 ? "allowedItemCategories" : "itemCategories"))
                        if (Find(itemCategoryKind, id.get<std::string>()) < 0)
                            throw std::runtime_error("Select an existing item category for each entry.");
                }
                if (updated["id"] != record["id"]) throw std::runtime_error("Record IDs are stable. Edit the display name instead.");
                records[selected] = std::move(updated);
                Select(category, selected);
                if (save) SaveLibrary(library);
                else status = "Preview updated. Save library to keep changes.";
            } catch (const std::exception& e) { status = e.what(); }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Preview pane", {0, 0}, ImGuiChildFlags_Borders);
        Preview(record, atlas);
        ImGui::EndChild();
    }
    ImGui::End();
}
