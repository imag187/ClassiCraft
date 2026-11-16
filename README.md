# ClassiCraft

A player-run modular MMO prototype. The target stack is C++ across Studio, the game client, and the server, with Dear ImGui for application windows. The native Studio and game prototypes share a Windows Direct3D 11 host. The playtest still uses a TypeScript server and a JavaScript world fixture generator.

## World authoring and rendering

Studio is where we define biomes, world units and elevation, gameplay generation rules, and the assets those rules place. Keep those definitions and the generated layout usable by either renderer. The current playtest uses an isometric view while the runtime rendering choice remains open.

- **Real-time 3D.** Studio would publish meshes, materials, and animation clips. Depth, changing viewpoints, and lighting can use the scene geometry, with runtime costs to measure on representative terrain and crowds.
- **Fixed-camera isometric sprites.** Studio would author or bake directional frames, pack animation atlases, and publish frame timings, ground pivots, and footprints. Measure texture memory and overdraw alongside frame time, and check depth sorting at walls and elevation changes.

Use the same generated biome, world units, assets, and playable loop to compare both approaches. Include an animated character, dense scenery, and a path behind tall objects. Compare the look and frame time at the target zoom alongside how long it takes to create, preview, and revise the assets in Studio.

## Run one native client

Build both Windows executables from the checkout root using the Visual Studio C++ desktop toolchain and CMake.

```powershell
cmake -S studio -B studio/build -A x64
cmake --build studio/build --config Release
```

Start the local server in a terminal.

```powershell
cd server
npm install
npm start
```

Open `studio/build/Release/ClassiCraftGame.exe`. The game connects to `ws://127.0.0.1:2570` by default. Set `CLASSICRAFT_WS_URL` before launching to choose another server.

- **WASD** moves relative to the screen. Hold **Shift** to sprint.
- Walk up to the Amber shard and press **E** to pick it up.
- Right-click the training dummy and press **Space** within range to cast Smite.

World geometry and Studio pixel art are cached by SHA-256 in `%LOCALAPPDATA%/ClassiCraft/world-cache`. The game downloads them when the published version changes or a local copy is missing or invalid. Player positions and pickup availability come from the current session. Pickups reset when the server restarts.

## Edit the playtest world

The server reads [The Borderlands](worlds/test-world/README.md) at startup. To open that same file in Studio, run this from the checkout root.

```powershell
$env:CLASSICRAFT_WORLD_PATH = (Resolve-Path .\worlds\test-world\world-project.json).Path
.\studio\build\Release\ClassiCraftStudio.exe
```

Use **World tools** to edit and save. Restart the server to publish the updated world. Set the same environment variable in the server terminal if you want it to use another saved world. The `client/project.godot` project is the earlier connection prototype.
