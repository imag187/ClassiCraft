# ClassiCraft Studio

A native Windows prototype for authoring RPG content. Requires the Visual C++ desktop toolchain, the Windows SDK, CMake, and Git. CMake fetches pinned Dear ImGui and nlohmann/json dependencies on the first configure.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\ClassiCraftStudio.exe
```

The app opens with a Death Knight example. Use **New** in any library section to author a record. Use **Design** to edit a definition, then **Save changes** to persist it. **Update preview** applies the definition to the open library so you can try it before saving. Use **Advanced JSON** for direct editing, followed by **Apply JSON** and **Save library**. Studio writes `library.json` beside the executable. You can edit this file in a text editor and use **Reload saved** to see your changes. Copy it elsewhere to keep a backup or share it. Record IDs are stable references between definitions.

The preview uses the applied data. You can select a progression profile, inspect unlocks at different levels, follow ability links, and spend preview points to check talent prerequisites. The talent preview shows nodes and prerequisites as a list. It does not run a combat simulation.

## Shared equipment slots

The library defines one equipment layout for the world. Add slots in **Equipment slots** and use a count greater than one for repeated positions such as rings. Each slot can reference eligible types from **Item categories**. Every class inherits the slot layout, and its optional category list narrows what it can equip. An empty category list imposes no restriction. The class preview shows the resulting categories for every slot. References use stable IDs, so renaming a category updates the display without breaking slot or class links.

When Studio opens a version 1 library, it combines its class equipment names into a shared slot list. Save to persist the upgrade. Version 2 free-text categories are promoted to library definitions, preserving their names and exact matches. Legacy slots have unrestricted categories until you set them.

Create an item category with a name and an optional group, such as Crystal in the Armor group. Pick it from the category dropdown in an equipment slot or class. The group is descriptive. Equipping an item in the future will also require its intended slot to match.

## Portable Windows build

Extract the Windows x64 ZIP to a folder you can write to, then open ClassiCraftStudio.exe. Keep example.json and the assets folder beside the executable. Save changes writes library.json in the same folder. Copy library.json with the app to continue the same content library on another PC.

The portable build includes the Visual C++ runtime. Install the development toolchain only to rebuild or change the C++ editor.

## World tools prototype

Choose **World tools** at the top of Studio. The world project is separate from the content library and is saved as `world-project.json` next to the executable. Use **Save world** to persist it; copy that file to back up or transfer the project. Reload is disabled while there are unsaved changes.

The starter test world includes a 20 by 20 grass map, grass/snow/path tiles, a player spawn, a training dummy, and a test wall. The tile editor paints 16 by 8 RGBA pixels (left click paints, right click erases). The map editor creates maps up to 64 by 64 cells, places tiles in an isometric view, and stores an explicit z-layer and collision flag per cell. The world editor places map references and records object positions and collision flags. Positions can be entered numerically or set on its isometric canvas.

This is authoring data only: there is no sprite editor, server export, collision simulation, or playable game client yet. World objects are shown as markers. The JSON schema is an early prototype and may change; preserve backups of authored projects.
