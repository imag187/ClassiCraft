# The Borderlands

A 56 × 44 isometric playtest world with a training area, woodland, highland, and ruins. The Studio project includes brick, oak and pine planks, trees, berry bushes, and granite rock artwork. Materials and biome variants are authoring data for future gameplay.

Use Studio’s **Tile editor** to paint 16 × 8 pixel artwork. In **World editor**, assign it through an object's **Artwork** field. An object with kind `item` and collision disabled can be picked up in the native client. See the [root README](../../README.md) for opening this file directly in Studio and running the game.

Studio Z layers raise artwork in the native client. A raised tile with collision checked draws solid side faces. Player movement, targeting, and pickups use the ground plane; upper layers can supply scenery and occlusion. Partially transparent pixels keep their alpha in the game.

To rebuild the generated playtest layout, run `node worlds/test-world/build-world.mjs` from the checkout root. The generator replaces the map layout, generated tiles, and placed scenery, so save any manual variants to a separate world file first.
