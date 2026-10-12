// Inferno (de_inferno, CS2), built by hand: rooms, streets, stairs and doorways as deliberate areas on the 16-unit
// grid, at CS2's real size and coordinates (+x east, +y north). Positions and heights were measured against CS2's
// radar and the community nav mesh (references only: nothing of Valve's ships); the owner's personal-testing
// exception, like Dust (CLAUDE.md rule 5). Later areas override earlier ones; "WALL" carves a wall back out
// (pillars, inner walls). Edges sit on multiples of 16. Included once, inside world.cpp's anonymous namespace.
#pragma once

constexpr float kInfernoCell = 16;  // (thin walls and exact doorways)

const DustArea kInfernoAreas[] = {
    // ---- T spawn: the yard, its way east to the T ramp, its way south-east to T side ----
    {"T SPAWN", -1744, -16, -1456, 752, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1744, -64, -1680, -16, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1456, 400, -1424, 752, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1648, 752, -1456, 848, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1600, -176, -1456, -16, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1456, 64, -1328, 464, -60, -60, 0, kIStreet, 0},
    {"WALL", -1456, 288, -1408, 384, 0, 0, 0, 0, 0},
    {"T SPAWN", -1328, 112, -1168, 320, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1344, 320, -1168, 608, -60, -60, 0, kIStreet, 0},
    {"T SPAWN", -1168, 464, -880, 608, -64, -28, 'x', kIStreet, 0},
    {"T SPAWN", -1456, -144, -1296, -32, -56, 4, 'x', kIStreet, 0},
    // ---- T side: the square between T spawn and the alley, its colonnade's pillars ----
    {"T SIDE", -1296, -400, -1168, -32, 4, 4, 0, kIStreet, 0},
    {"T SIDE", -1168, -496, -1040, -272, 4, 4, 0, kIStreet, 0},
    {"T SIDE", -1296, -464, -1168, -400, 4, 4, 0, kIStreet, 0},
    {"T SIDE", -1040, -496, -656, 256, 4, 4, 0, kIStreet, 0},
    {"T SIDE", -880, 256, -656, 464, 4, -24, 'y', kIStreet, 0},
    {"WALL", -912, -32, -880, 64, 0, 0, 0, 0, 0},
    {"WALL", -912, -304, -880, -208, 0, 0, 0, 0, 0},
    // ---- T ramp: up from T side and T spawn towards bottom mid; the ledge behind it ----
    {"T RAMP", -880, 464, -384, 800, -24, -24, 0, kIStreet, 0},
    {"T RAMP", -592, 800, -528, 928, 8, 8, 0, kIStreet, 0},
    {"T RAMP", -528, 800, -464, 928, 30, 30, 0, kIStreet, 0},
    {"T RAMP", -464, 768, 0, 928, -16, 84, 'x', kIStreet, 0},
    {"BOTTOM MID", 0, 416, 192, 1152, 84, 84, 0, kIStreet, 0},
    {"BOTTOM MID", 192, 416, 256, 528, 84, 84, 0, kIStreet, 0},
    {"BOTTOM MID", 192, 528, 208, 736, 84, 84, 0, kIStreet, 0},
    {"BOTTOM MID", 320, 416, 384, 736, 84, 84, 0, kIStreet, 0},
    // ---- Banana: up from bottom mid (the logs' cubby on the left), the long diagonal past sandbags to the car ----
    {"BANANA", 48, 896, 192, 1440, 85, 105, 'y', kIDirt, 0},
    {"BANANA", -96, 1328, 48, 1440, 105, 105, 0, kIDirt, 0},
    {"BANANA", 192, 1232, 288, 1440, 104, 110, 'y', kIDirt, 0},
    {"BANANA", 64, 1440, 288, 1504, 105, 110, 'y', kIDirt, 0},
    {"BANANA", 96, 1504, 320, 1568, 110, 115, 'y', kIDirt, 0},
    {"BANANA", 128, 1568, 336, 1632, 115, 120, 'y', kIDirt, 0},
    {"BANANA", 144, 1632, 368, 1696, 120, 125, 'y', kIDirt, 0},
    {"BANANA", 176, 1696, 400, 1760, 125, 130, 'y', kIDirt, 0},
    {"BANANA", 208, 1760, 432, 1824, 130, 135, 'y', kIDirt, 0},
    {"BANANA", 240, 1824, 464, 1888, 135, 140, 'y', kIDirt, 0},
    {"CAR", 256, 1888, 928, 1952, 140, 140, 0, kIDirt, 0},
    {"CAR", 384, 1952, 928, 2128, 140, 140, 0, kIDirt, 0},
    {"CAR", 928, 1840, 1056, 1904, 148, 148, 0, kIDirt, 0},
    {"CAR", 688, 2128, 944, 2224, 140, 140, 0, kIDirt, 0},
    {"CAR", 688, 2224, 880, 2624, 140, 140, 0, kIDirt, 0},
    // ---- B site: the fountain, box stacks, the way up from the banana side, the back (dark, coffins), the apse ----
    {"B SITE", -16, 2432, 608, 3168, 164, 164, 0, kIBrick, 0},
    {"B SITE", -16, 3168, 96, 3264, 164, 164, 0, kIBrick, 0},
    {"B SITE", 576, 2432, 688, 2816, 164, 140, 'x', kIBrick, 0},
    {"B SITE", 608, 2816, 816, 2976, 164, 128, 'x', kIBrick, 0},
    {"B SITE", 208, 2608, 416, 2928, 196, 196, 0, kIStone, 0},
    {"B SITE", 176, 2672, 448, 2864, 196, 196, 0, kIStone, 0},
    {"COFFINS", 352, 3168, 624, 3520, 164, 164, 0, kIBrick, 0},
    {"COFFINS", 624, 3360, 816, 3520, 164, 130, 'x', kIDirt, 0},
    {"B SITE", 640, 3024, 800, 3264, 164, 164, 0, kIBrick, 0},
    {"CONSTRUCTION", 800, 3024, 896, 3264, 164, 130, 'x', kIDirt, 0},
    // ---- CT: out of B east, down towards CT and construction ----
    {"CT", 608, 2624, 1104, 2816, 157, 133, 'x', kIStreet, 0},
    {"CONSTRUCTION", 896, 3008, 1104, 3520, 130, 130, 0, kIDirt, 0},
    {"CONSTRUCTION", 816, 3264, 896, 3520, 130, 130, 0, kIDirt, 0},
    {"WALL", 576, 3024, 640, 3264, 0, 0, 0, 0, 0},   // the apse's curved wall: west, north and south arcs
    {"WALL", 576, 3264, 832, 3360, 0, 0, 0, 0, 0},
    {"WALL", 576, 2976, 832, 3024, 0, 0, 0, 0, 0},
    // ---- Mid: east from bottom mid, up to top mid ----
    {"MID", 384, 416, 960, 688, 87, 101, 'x', kIStreet, 0},
    {"MID", 528, 688, 960, 736, 91, 101, 'x', kIStreet, 0},
    {"TOP MID", 960, 416, 1312, 608, 101, 126, 'x', kIStreet, 0},
    // ---- The underpass: down from the alley's end under the buildings and bottom mid, up stairs into bottom mid ----
    {"UNDERPASS", 256, -64, 320, 144, 64, -4, 'y', kIStone, 100},
    {"UNDERPASS", 256, 144, 320, 368, -4, -4, 0, kIStone, 100},
    {"UNDERPASS", 256, 368, 320, 400, -4, -24, 'y', kIStone, 100},
    {"UNDERPASS", 256, 400, 320, 528, -24, -24, 0, kIStone, 0},      // (under bottom mid's deck: headroom)
    {"UNDERPASS", 224, 528, 320, 688, -24, 84, 'y', kIStone, 0},
    {"BOTTOM MID", 208, 688, 336, 736, 84, 84, 0, kIStreet, 0},      // the stairs' landing: off to either side
    {"WALL", 208, 528, 224, 688, 0, 0, 0, 0, 0},                       // the stairwell's walls (on and off at the top)
    {"WALL", 320, 528, 336, 688, 0, 0, 0, 0, 0},
    // ---- Second mid: the street east of the alley, up south to the loop round the block, north up to mid, and the
    // stairs up to the window by CT apps ----
    {"SECOND MID", 224, -144, 624, 16, 68, 74, 'x', kIStreet, 0},
    {"SECOND MID", 624, -144, 864, 16, 74, 90, 'x', kIStreet, 0},
    {"SECOND MID", 224, -272, 400, -144, 100, 68, 'y', kIStreet, 0},
    {"SECOND MID", 400, -304, 864, -144, 92, 76, 'y', kIStreet, 0},
    {"SECOND MID", 128, -528, 256, -400, 100, 100, 0, kIStreet, 0},
    {"SECOND MID", 256, -736, 368, -272, 100, 100, 0, kIStreet, 0},
    {"SECOND MID", 368, -736, 768, -592, 100, 92, 'x', kIStreet, 0},
    {"SECOND MID", 768, -800, 976, -304, 92, 92, 0, kIStreet, 0},
    {"SECOND MID", 704, 16, 848, 416, 82, 97, 'y', kIStreet, 0},
    {"SECOND MID", 848, 64, 960, 304, 200, 104, 'y', kIStreet, 0},
    {"SECOND MID", 848, 304, 960, 416, 100, 100, 0, kIStreet, 0},
    {"SECOND MID", 848, 32, 960, 64, 200, 200, 0, kIStreet, 0},
    // ---- CT apps: up its stairs from second mid's east end, the rooms (a raised one), boiler's stairs down to top mid,
    // close apps along to the balcony over pit ----
    {"SECOND MID", 976, -704, 1072, -608, 100, 130, 'x', kIStreet, 0},
    {"SECOND MID", 1072, -704, 1296, -608, 130, 130, 0, kIStreet, 0},
    {"CT APPS", 1168, -608, 1264, -400, 130, 260, 'y', kIWood, 112},
    {"CT APPS", 976, -304, 1296, 48, 260, 260, 0, kIWood, 112},
    {"CT APPS", 1168, -400, 1296, -304, 260, 260, 0, kIWood, 112},
    {"CT APPS", 1104, -256, 1168, -80, 292, 292, 0, kIWood, 80},
    {"BOILER", 1168, 48, 1296, 272, 250, 130, 'y', kIWood, 112},
    {"BOILER", 1168, 272, 1296, 320, 130, 130, 0, kIWood, 112},
    {"CLOSE APPS", 1296, -400, 1904, -272, 260, 260, 0, kIWood, 112},
    {"CLOSE APPS", 1808, -272, 2096, -160, 260, 260, 0, kIWood, 112},
    {"BALCONY", 1872, -160, 2064, -112, 260, 224, 'y', kIStone, 0},
    {"BALCONY", 1872, -112, 2064, 0, 224, 224, 0, kIStone, 0},
    // ---- Pit: below the balcony, up north into A site ----
    {"PIT", 2096, -528, 2624, -288, 100, 100, 0, kIDirt, 0},
    {"PIT", 2096, -288, 2624, 32, 100, 124, 'y', kIDirt, 0},
    // ---- A short, top mid, arch, A long: the ring round the buildings west and north of A site ----
    {"A SHORT", 1312, 0, 1488, 416, 130, 122, 'y', kIStreet, 0},
    {"A SHORT", 1344, -80, 1872, 0, 130, 130, 0, kIStreet, 0},
    {"A SITE", 1488, 0, 2432, 144, 130, 122, 'x', kIStreet, 0},
    {"TOP MID", 1248, 416, 1488, 640, 122, 122, 0, kIStreet, 0},
    {"ARCH", 1312, 640, 1488, 1024, 130, 155, 'y', kIStreet, 0},
    {"A LONG", 1360, 1024, 1536, 1248, 178, 178, 0, kIStreet, 0},
    {"A LONG", 1536, 1024, 1696, 1248, 178, 170, 'x', kIStreet, 0},
    {"A LONG", 1696, 1024, 2112, 1248, 170, 170, 0, kIStreet, 0},
    {"A LONG", 2112, 1024, 2272, 1248, 170, 178, 'x', kIStreet, 0},
    {"A LONG", 2272, 1024, 2464, 1248, 178, 178, 0, kIStreet, 0},
    {"ARCH", 1760, 1248, 1888, 1600, 168, 160, 'y', kIStreet, 0},
    // ---- A site: raised, the boxes and the truck, graveyard, the back of the site up to A long; library ----
    {"A SITE", 1792, 144, 2176, 704, 160, 160, 0, kISite, 0},
    {"A SITE", 2176, 144, 2208, 704, 140, 158, 'y', kISite, 0},
    {"A SITE", 2208, 144, 2432, 704, 124, 150, 'y', kISite, 0},
    {"A SITE", 2176, 704, 2432, 1024, 155, 165, 'y', kISite, 0},
    {"GRAVEYARD", 2400, 288, 2496, 384, 196, 196, 0, kIGrass, 0},
    {"LIBRARY", 2304, 1280, 2592, 1568, 160, 160, 0, kIWood, 140},
    {"LIBRARY", 2496, 1056, 2656, 1280, 160, 160, 0, kIWood, 140},
    {"LIBRARY", 2336, 1568, 2432, 1584, 160, 160, 0, kIWood, 140},   // its doors: to CT spawn's band, onto A long
    {"LIBRARY", 2464, 1104, 2496, 1200, 178, 160, 'x', kIWood, 140},
    // ---- CT: from B's way out east along the north, into CT spawn; construction's east end ----
    {"CT", 1104, 2600, 1376, 2744, 133, 130, 'x', kIStreet, 0},
    {"CT", 1104, 2744, 2240, 3008, 130, 130, 0, kIStreet, 0},
    {"CONSTRUCTION", 1104, 3008, 1344, 3232, 130, 130, 0, kIDirt, 0},
    {"CONSTRUCTION", 848, 3264, 1056, 3472, 130, 130, 0, kIDirt, 0},
    // ---- Speedway: from CT down the west side of the middle block to the arch, a raised stretch half way ----
    {"SPEEDWAY", 1568, 2648, 1712, 2744, 160, 128, 'y', kIStreet, 0},
    {"SPEEDWAY", 1568, 2224, 1712, 2648, 160, 160, 0, kIStreet, 0},
    {"SPEEDWAY", 1568, 2160, 1712, 2224, 180, 160, 'y', kIStreet, 0},
    {"SPEEDWAY", 1568, 1904, 1712, 2160, 180, 180, 0, kIStreet, 0},
    {"SPEEDWAY", 1568, 1856, 1712, 1904, 160, 180, 'y', kIStreet, 0},
    // ---- CT spawn: the yard east of the middle block, its east side down to the south band; the band from speedway
    // and the arch past library ----
    {"CT SPAWN", 1712, 2224, 2240, 2944, 130, 130, 0, kIStreet, 0},
    {"CT SPAWN", 2240, 2224, 2528, 2624, 130, 130, 0, kIStreet, 0},
    {"CT SPAWN", 2240, 2624, 2400, 2816, 130, 130, 0, kIStreet, 0},
    {"CT SPAWN", 2192, 1872, 2528, 2224, 130, 130, 0, kIStreet, 0},
    {"CT SPAWN", 1568, 1584, 1952, 1920, 160, 160, 0, kIStreet, 0},
    {"CT SPAWN", 1952, 1584, 2096, 1920, 160, 144, 'x', kIStreet, 0},
    {"CT SPAWN", 2096, 1584, 2480, 1920, 160, 140, 'y', kIStreet, 0},
    // ---- The alley: from T side east under the T apps bridge to second mid ----
    {"ALLEY", -656, -256, -448, 16, 0, 26, 'x', kIStreet, 0},
    {"ALLEY", -448, -256, -160, 16, 26, 26, 0, kIStreet, 0},
    {"ALLEY", -160, -208, 32, 16, 26, 46, 'x', kIStreet, 0},
    {"ALLEY", 32, -208, 96, 16, 46, 46, 0, kIStreet, 0},
    {"ALLEY", 96, -208, 224, 16, 46, 68, 'x', kIStreet, 0},
    // ---- T apps: up the stairs from the T ramp, the rooms, the bridge over the alley (a deck), the south rooms and
    // the stairs down to second mid ----
    {"T APPS", -592, 464, -528, 608, 44, -28, 'y', kIWood, 0},
    {"T APPS", -528, 464, -432, 592, 44, 44, 0, kIWood, 0},
    {"T APPS", -592, 384, -432, 464, 44, 44, 0, kIWood, 112},
    {"T APPS", -592, 144, -528, 384, 196, 44, 'y', kIWood, 112},
    {"T APPS", -608, 48, -480, 144, 196, 196, 0, kIWood, 112},
    {"T APPS", -480, 48, 48, 400, 196, 196, 0, kIWood, 112},
    {"T APPS", -128, 16, 256, 176, 196, 212, 'x', kIWood, 112},
    {"T APPS", -448, 16, -320, 48, 196, 196, 0, kIWood, 112},
    {"WALL", -320, 80, -272, 256, 0, 0, 0, 0, 0},   // a block in the north rooms, and a closet open to the north
    {"WALL", -208, 64, -192, 240, 0, 0, 0, 0, 0},
    {"WALL", -160, 64, -144, 240, 0, 0, 0, 0, 0},
    {"WALL", -208, 48, -144, 96, 0, 0, 0, 0, 0},
    {"T APPS", -464, -512, -208, -256, 196, 196, 0, kIWood, 112},
    {"T APPS", -448, -624, -336, -512, 196, 196, 0, kIWood, 112},
    {"T APPS", -208, -528, -48, -400, 196, 196, 0, kIWood, 112},
    {"T APPS", -48, -528, 128, -400, 196, 100, 'x', kIWood, 112},
};

// The T apps bridge over the alley (and anything else that's a floor over a floor).
const TownDeck kInfernoDecks[] = {
    {"T APPS BRIDGE", -448, -256, -320, 16, 196, 44, 112},
    {"UNDERPASS DECK", 256, 416, 320, 528, 84, 0, 0},  // bottom mid's floor over the underpass
};

// Things standing in the open: x0 y0 x1 y1 and height.
const float kInfernoSmallProps[][5] = {
    {-1040, 208, -1008, 256, 56},  // T side: a crate by the north wall, barrels about
    {-1088, -304, -1056, -272, 44},
    {-1296, -288, -1264, -256, 44},
    {-1744, 80, -1712, 104, 44},   // T spawn: barrels along the west wall
    {-1744, 208, -1712, 240, 44},
    {-1744, 296, -1712, 320, 44},
    {48, 1456, 104, 1528, 40},     // banana: the logs at the bottom, sandbags half way up, the car at the top
    {392, 1776, 464, 1848, 40},
    {632, 1856, 744, 1920, 56},
    {352, 2016, 480, 2080, 40},    // top of banana: barrels and crates
    {760, 3208, 800, 3280, 36},    // B: a coffin in the apse
    {408, -736, 448, -704, 40},    // second mid: benches
    {408, -600, 448, -568, 40},
    {976, 2624, 1120, 2656, 36},   // CT: a planter along the way out of B
    {2512, 1040, 2608, 1072, 72},  // library: the bookshelves
    {2624, 1104, 2656, 1200, 72},
    {2448, 1520, 2560, 1552, 72},
    {1968, 2448, 2048, 2544, 40},  // CT spawn: crates by the cart
    {336, 2752, 368, 2784, 220},   // B: the fountain's statue
    {64, 2560, 128, 2704, 64},     // B: boxes along the west wall
    {64, 2896, 128, 2976, 64},
    {360, 2416, 424, 2512, 120},   // B: crates on the banana side (two high)
    {496, 2416, 560, 2592, 118},   // B: the stack by the way in from banana (three steps up)
    {560, 2416, 624, 2480, 80},
    {560, 2544, 624, 2608, 32},
    {112, 3056, 240, 3168, 140},   // B: the tall stack at the back (dark)
    {480, 2992, 560, 3184, 100},   // B: crates by the apse
    {560, 2864, 640, 2944, 40},
    {2032, 656, 2112, 744, 60},    // A: the boxes and the truck
    {1920, 384, 2048, 480, 60},
    {1792, 512, 1888, 608, 60},
    {2016, 192, 2144, 320, 110},
    {1792, 192, 1920, 288, 60},
    {1952, 224, 2016, 288, 50},
    {1632, 112, 1760, 160, 50},    // A short's exit: a low box
    {1872, 2512, 1968, 2608, 90},  // CT spawn: the cart
    {1904, 2400, 2000, 2496, 56},
    {2048, 2208, 2144, 2256, 30},
};
