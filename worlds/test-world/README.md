# ClassiCraft test world

This is the editable starter world for the Studio prototype. It contains a test map, three simple tiles, a player spawn, a training dummy, and a wall. Keep it under version control so it can move between development machines.

Studio currently reads and saves `world-project.json` next to its executable. To use this tracked world, close Studio and copy this directory's `world-project.json` beside the Studio executable, preserving a backup of any existing file there first. Open Studio and choose **World tools**. After editing, choose **Save world**, close Studio, and copy the saved file back here. Review the diff and commit it along with any changes you want to share. These are separate copies; saving in Studio does not automatically update this repository.

Do not commit private world projects or assets without their owners' permission. This is an early prototype; gameplay, sprite assets and a C++ server are not implemented in this file.
