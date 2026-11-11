# ClassiCraft Studio 0.1.0 preview

The first native Windows x64 preview of ClassiCraft Studio introduces an RPG content library for classes, spells and abilities, talent trees, progression profiles, equipment slots, and item categories.

- Browse or search the library, pick icons visually, and follow links between definitions.
- Preview Death Knight ability unlocks at different levels and spend preview talent points to test prerequisites.
- Define shared equipment slots and create custom item categories such as Crystal armor. Classes can restrict which categories they equip.
- Edit through the Design tab or Advanced JSON. Existing v1 and v2 libraries upgrade when opened.

## Run on Windows

Extract `ClassiCraft-Studio-win-x64.zip` to a writable folder and open `Studio\ClassiCraftStudio.exe`. The ZIP includes the sample library, pixel-art icons, and C++ source. No developer toolchain or separate Visual C++ runtime is needed to run the editor. Copy `Studio\library.json` between machines to carry your edits.

This is an authoring preview. The Release build and launch from a fresh ZIP extraction were verified on the build PC. Equipment migration checks passed. The full interactive edit-and-save workflow and launch on a second PC remain unverified.
