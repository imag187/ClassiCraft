// Compile alongside Dear ImGui sources; includes the editor to exercise its internal project routines.
#include "../world_editor.cpp"
#include <cassert>
#include <filesystem>

int main(int argc, char** argv) {
    project = DefaultProject();
    assert(project.at("tiles").size() == 3);
    assert(project.at("maps").at(0).at("cells").size() == 400);
    assert(project.at("world").at("objects").size() == 3);
    const auto before = project;
    assert(argc == 2);
    const std::filesystem::path path = argv[1];
    assert(!std::filesystem::exists(path));
    Save(path);
    assert(!dirty);
    project = Json::object();
    Load(path);
    assert(!loadFailed && project == before);
    std::filesystem::remove(path);
    return 0;
}
