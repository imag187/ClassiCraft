import { readFileSync, writeFileSync } from "node:fs";

const file = new URL("world-project.json", import.meta.url);
const project = JSON.parse(readFileSync(file, "utf8"));
const map = project.maps.find(({ id }) => id === "test-room");
const width = 56;
const height = 44;
const packed = (rgb, alpha = 255) => ((alpha << 24) + ((rgb & 255) << 16) + (rgb & 0x00ff00) + (rgb >>> 16)) >>> 0;
const diamond = project.tiles.find(({ id }) => id === "grass").pixels;
const tiles = project.tiles.filter(({ id }) => ["grass", "path", "snow", "amber-shard"].includes(id));

function floor(id, name, material, biome, pattern) {
  tiles.push({
    id, name, material, biome,
    pixels: diamond.map((pixel, i) => pixel ? packed(pattern(i % 16, Math.floor(i / 16))) : 0),
  });
}

function prop(id, name, material, biome, palette, rows) {
  if (rows.length !== 8 || rows.some(row => row.length !== 16)) throw new Error("Invalid tile artwork " + id);
  tiles.push({ id, name, material, biome, pixels: rows.flatMap(row =>
    [...row].map(pixel => pixel === "." ? 0 : packed(palette[pixel], pixel === "s" ? 90 : 255))) });
}

function bricks(x, y, stone, highlight, mortar) {
  if (y % 3 === 0 || (x + 3 * Math.floor(y / 3)) % 6 === 0) return mortar;
  return y % 3 === 1 ? highlight : stone;
}

function planks(x, y, board, highlight, seam) {
  if (x % 4 === 0 || (y + (x % 3) * 2) % 7 === 0) return seam;
  return (x + y * 3) % 7 < 2 ? highlight : board;
}

floor("ruin-brick", "Weathered brick", "brick", "ruin",
  (x, y) => bricks(x, y, 0x655e63, 0x82777b, 0x443e48));
floor("oak-planks", "Oak planks", "oak", "woodland",
  (x, y) => planks(x, y, 0x916244, 0xb27d54, 0x67442e));
floor("pine-planks", "Pine planks", "pine", "highland",
  (x, y) => planks(x, y, 0xa49472, 0xc3b391, 0x706854));
floor("woodland-grass", "Woodland grass", "grass", "woodland",
  (x, y) => (x * 7 + y * 5 + (x * y) % 9) % 17 < 4 ? 0x527b52 : 0x486d4a);
floor("highland-stone", "Highland scree", "stone", "highland",
  (x, y) => (x * 3 + y * 5) % 13 < 3 ? 0x89959c : 0x65737d);

prop("meadow-detail", "Meadow detail", "grass", "woodland",
  { s: 0x9bb276, h: 0x9bb276, m: 0x657f55 }, [
    "................", "................", "........h.......", "......s.hs......",
    ".....hshhs......", ".....hmmh....s..", "......mm....s...", "................",
  ]);

const tree = [
  ".......oo.......", "......ohho......", ".....ohhhho.....", "....ohhmhhho....",
  "...ohmmhmhmmo...", "..ohmmdmmmdmmo..", "...odddddddo....", "......obb.......",
];
prop("oak-tree", "Oak tree", "oak", "woodland",
  { o: 0x263a31, h: 0x79a35d, m: 0x4f7848, d: 0x35583c, b: 0x865937 }, tree);
prop("pine-tree", "Highland pine", "pine", "highland",
  { o: 0x283940, h: 0xa2b9b7, m: 0x527c73, d: 0x395d58, b: 0x846951 }, [
    ".......oo.......", "......ohmo......", ".....ohhmmo.....", "......hmmo......",
    "....ohhmmmmo....", "...ohmdmmmdmo...", "..oddmmmmdddoo..", "......obb.......",
  ]);
prop("berry-bush", "Berry bush", "berries", "woodland",
  { o: 0x293b31, h: 0x789952, m: 0x4d743e, d: 0x9d4456, s: 0x789952 }, [
    "................", "................", ".....sohhos.....", "...sohhmhmmoo...",
    "..ohhdmmhdmmo...", "..ohmmhmmmdmmo..", "...ommmmmmmo....", "....oooooooo....",
  ]);
prop("granite-rock", "Granite rock", "granite", "highland",
  { o: 0x384550, h: 0xb4c0c1, m: 0x778791, d: 0x576675 }, [
    "................", "................", "......oooo......", "....oohhhmoo....",
    "...ohhhhmmmdo...", "..ohmmmmmdddoo..", "...ommmddddo....", "....oooooooo....",
  ]);

const objects = project.world.objects.filter(({ kind, artwork }) =>
  kind === "spawn" || kind === "enemy" || artwork === "amber-shard");
const put = (x, y, artwork, collision = true) => {
  const { name, material, biome } = tiles.find(tile => tile.id === artwork);
  objects.push({ x, y, z: 0, kind: "object", name, artwork, collision, material, biome });
};

const cells = [];
for (let y = 0; y < height; y++) {
  for (let x = 0; x < width; x++) {
    const highland = y >= 24;
    const ruin = x >= 34 && y < 24;
    const road = (y === 9 && x >= 3 && x < 51) || (x === 16 && y >= 9 && y < 38)
      || (y === 30 && x >= 16 && x < 49) || (x === 3 && y >= 3 && y <= 9);
    const perimeter = x === 0 || y === 0 || x === width - 1 || y === height - 1;
    const ruinWall = ruin && ((x === 34 && y !== 9 && y !== 10) || (y === 20 && x < 51 && x !== 44));
    const trainingWall = x === 12 && y >= 2 && y <= 14 && y !== 9 && y !== 10;
    const collision = perimeter || ruinWall || trainingWall;
    const deck = (x >= 20 && x <= 23 && y >= 8 && y <= 11)
      || (x >= 27 && x <= 30 && y >= 29 && y <= 32);
    let tile = highland ? "highland-stone" : ruin ? "ruin-brick" : "woodland-grass";
    if (road) tile = ruin ? "ruin-brick" : "path";
    if (deck) tile = highland ? "pine-planks" : "oak-planks";
    if (trainingWall) tile = "ruin-brick";
    cells.push({ collision, tile, x, y, z: 0 });
    const seed = ((x * 73856093) ^ (y * 19349663)) >>> 0;
    if (trainingWall || (ruinWall && !perimeter)) {
      cells.push({ collision: true, tile: "ruin-brick", x, y, z: ruinWall ? 3 : 2 });
    } else if (!collision && !road && !deck && !ruin && !highland && seed % 9 === 0) {
      cells.push({ collision: false, tile: "meadow-detail", x, y, z: 1 });
    }
    if (collision || road || deck || ruin || (x < 13 && y < 12)) continue;
    if (seed % 29 === 0) put(x, y, highland ? "pine-tree" : "oak-tree");
    else if (seed % 41 === 0) put(x, y, highland ? "granite-rock" : "berry-bush");
  }
}

const blocked = new Set([...cells, ...objects].filter(cell => cell.collision).map(({ x, y }) => y * width + x));
const reachable = new Set([3 * width + 3]);
const frontier = [...reachable];
for (let index = 0; index < frontier.length; index++) {
  const cell = frontier[index];
  for (const next of [cell - 1, cell + 1, cell - width, cell + width]) {
    if (blocked.has(next) || reachable.has(next)) continue;
    reachable.add(next);
    frontier.push(next);
  }
}
for (const [x, y] of [[5, 4], [8, 9], [21, 9], [47, 9], [29, 30], [47, 30]]) {
  if (!reachable.has(y * width + x)) throw new Error(`Unreachable playtest landmark (${x}, ${y})`);
}

Object.assign(map, { name: "The Borderlands", width, height, cells });
project.tiles = tiles;
project.world.name = "ClassiCraft Borderlands";
project.world.objects = objects;
writeFileSync(file, JSON.stringify(project, null, 2) + "\n");
