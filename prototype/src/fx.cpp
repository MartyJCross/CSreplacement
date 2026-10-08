#include "fx.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "items.h"

namespace {

// Colours. The gun "roles" (metal, light metal, furniture, dark furniture, polymer) are what a skin paints;
// gloves, sleeves and brass never are. Blades: steel, the bright edge bevel and the darker fuller.
constexpr uint32_t kMetal = 0x2b2e33, kMetalLight = 0x41454c, kWood = 0x7a5230, kWoodDark = 0x5a3c23,
                   kGlove = 0x2a2a2a, kSleeve = 0x4f5b3f;
constexpr uint32_t kPolymer = 0x26282b, kOlive = 0x4c5a35, kOliveDark = 0x3a4528;
constexpr uint32_t kBlade = 0xb9c0c8, kBladeEdge = 0xdfe3e8, kBladeFuller = 0x8a9098;
constexpr uint32_t kSilver = 0x9aa0a8;

struct Part { Vec3 mins, maxs; uint32_t color; };

// Rifle in gun-local space: x right, y up, z back (muzzle points to -z). Units ~ inches.
const Part kRifleBody[] = {
    {{-0.9f, -1.1f, -6.0f}, {0.9f, 1.1f, 6.0f}, kMetal},          // receiver
    {{-0.8f, 1.1f, -5.0f}, {0.8f, 1.6f, 5.5f}, kMetalLight},      // dust cover
    {{-0.5f, 1.6f, -4.5f}, {0.5f, 2.2f, -3.5f}, kMetal},          // rear sight
    {{-1.05f, -1.2f, -13.0f}, {1.05f, 0.6f, -6.0f}, kWood},       // lower handguard
    {{-0.7f, 0.6f, -12.5f}, {0.7f, 1.3f, -6.0f}, kWoodDark},      // upper handguard
    {{-0.32f, -0.1f, -22.0f}, {0.32f, 0.55f, -13.0f}, kMetal},    // barrel
    {{-0.45f, 0.55f, -20.5f}, {0.45f, 2.0f, -19.5f}, kMetal},     // front sight
    {{-0.5f, -0.3f, -23.5f}, {0.5f, 0.75f, -22.0f}, kMetalLight}, // muzzle brake
    {{-0.2f, -2.0f, 0.2f}, {0.2f, -1.1f, 2.8f}, kMetal},          // trigger guard
    {{-0.65f, -4.6f, 2.6f}, {0.65f, -1.1f, 4.6f}, kWoodDark},     // pistol grip
    {{-0.75f, -1.6f, 6.0f}, {0.75f, 1.0f, 15.0f}, kWood},         // stock
    {{-0.75f, -3.2f, 10.0f}, {0.75f, -1.6f, 15.0f}, kWood},       // stock heel
};
const Part kRifleArms[] = {
    {{-1.3f, -4.2f, 2.2f}, {1.4f, -1.4f, 5.0f}, kGlove},          // right hand
    {{0.6f, -6.5f, 5.0f}, {3.6f, -3.2f, 14.0f}, kSleeve},         // right forearm
    {{-1.5f, -2.4f, -11.5f}, {1.5f, -0.6f, -8.0f}, kGlove},       // left hand
    {{-5.5f, -6.5f, -9.5f}, {-1.2f, -2.6f, -3.0f}, kSleeve},      // left forearm
};
const Part kRifleMag[] = {
    {{-0.65f, -5.0f, -4.2f}, {0.65f, -1.1f, -1.4f}, kMetal},
    {{-0.65f, -8.2f, -5.6f}, {0.65f, -4.6f, -2.6f}, kMetal},
};
const Part kPistolBody[] = {
    {{-0.55f, 0.2f, -8.5f}, {0.55f, 1.5f, 1.0f}, kMetal},         // slide
    {{-0.5f, -0.6f, -7.5f}, {0.5f, 0.2f, 0.5f}, kPolymer},        // frame
    {{-0.25f, 0.5f, -9.0f}, {0.25f, 1.1f, -8.5f}, kMetalLight},   // barrel crown
    {{-0.1f, 1.5f, -8.0f}, {0.1f, 1.8f, -7.6f}, kMetal},          // front sight
    {{-0.4f, 1.5f, 0.2f}, {0.4f, 1.8f, 0.8f}, kMetal},            // rear sight
    {{-0.55f, -4.2f, -1.0f}, {0.55f, -0.6f, 1.2f}, kPolymer},     // grip
    {{-0.15f, -1.4f, -3.0f}, {0.15f, -0.6f, -0.8f}, kPolymer},    // trigger guard
};
const Part kPistolArms[] = {
    {{-1.2f, -4.0f, -1.4f}, {1.3f, -1.0f, 1.6f}, kGlove},         // right hand
    {{-1.6f, -4.2f, -2.0f}, {-0.4f, -1.2f, 1.2f}, kGlove},        // left hand (support grip)
    {{0.4f, -6.5f, 1.6f}, {3.2f, -3.5f, 11.0f}, kSleeve},         // right forearm
    {{-4.5f, -6.8f, 1.0f}, {-1.0f, -3.8f, 10.0f}, kSleeve},       // left forearm
};
const Part kPistolMag[] = {
    {{-0.45f, -4.5f, -0.8f}, {0.45f, -0.7f, 1.0f}, kMetalLight},
};

const Part kSniperBody[] = {
    {{-0.8f, -1.0f, -5.0f}, {0.8f, 1.0f, 6.0f}, kOlive},             // receiver
    {{-1.0f, -1.2f, -14.0f}, {1.0f, 0.4f, -5.0f}, kOlive},           // forend
    {{-0.35f, -0.1f, -30.0f}, {0.35f, 0.55f, -14.0f}, kMetal},       // barrel
    {{-0.55f, -0.3f, -32.0f}, {0.55f, 0.75f, -30.0f}, kMetalLight},  // muzzle brake
    {{-0.9f, -2.6f, 6.0f}, {0.9f, 1.2f, 17.0f}, kOlive},             // stock
    {{-0.65f, -4.2f, 5.0f}, {0.65f, -1.0f, 7.0f}, kOliveDark},       // grip
    {{-0.75f, 1.6f, -6.0f}, {0.75f, 3.0f, 5.0f}, kMetal},            // scope tube
    {{-1.0f, 1.4f, -8.0f}, {1.0f, 3.2f, -6.0f}, kMetal},             // objective bell
    {{-0.9f, 1.5f, 5.0f}, {0.9f, 3.1f, 6.5f}, kMetal},               // eyepiece
    {{-0.4f, 1.0f, -3.0f}, {0.4f, 1.6f, -2.0f}, kMetalLight},        // front mount
    {{-0.4f, 1.0f, 2.0f}, {0.4f, 1.6f, 3.0f}, kMetalLight},          // rear mount
    {{0.8f, 0.2f, 2.0f}, {2.2f, 0.6f, 2.6f}, kMetalLight},           // bolt handle
};
const Part kSniperArms[] = {
    {{-1.3f, -4.0f, 4.5f}, {1.4f, -1.2f, 7.5f}, kGlove},             // right hand
    {{0.6f, -6.5f, 7.5f}, {3.6f, -3.2f, 16.0f}, kSleeve},            // right forearm
    {{-1.5f, -2.4f, -12.0f}, {1.5f, -0.6f, -8.5f}, kGlove},          // left hand
    {{-5.5f, -6.5f, -10.0f}, {-1.2f, -2.6f, -3.5f}, kSleeve},        // left forearm
};
const Part kSniperMag[] = {
    {{-0.6f, -3.2f, -3.0f}, {0.6f, -1.0f, 0.5f}, kMetal},
};

// Dual Berettas: two pistols, the right at x 0 and the left 8.5 inches over. Silver slides with the barrel
// showing through the open top, dark frames, wooden grips.
constexpr float kLeftGun = -8.5f;
#define BERETTA(xo)                                                                                  \
    {{xo - 0.5f, 0.75f, -8.0f}, {xo + 0.5f, 1.4f, 0.8f}, kMetalLight},     /* slide (open top) */   \
    {{xo - 0.5f, 0.2f, -7.6f}, {xo - 0.2f, 0.75f, 0.8f}, kMetalLight},                            \
    {{xo + 0.2f, 0.2f, -7.6f}, {xo + 0.5f, 0.75f, 0.8f}, kMetalLight},                            \
    {{xo - 0.22f, 0.3f, -8.7f}, {xo + 0.22f, 0.95f, -0.5f}, kMetal},       /* barrel */             \
    {{xo - 0.48f, -0.6f, -7.0f}, {xo + 0.48f, 0.2f, 0.6f}, kMetal},        /* frame */              \
    {{xo - 0.52f, -4.1f, -0.9f}, {xo + 0.52f, -0.6f, 1.2f}, kWoodDark},    /* grip */               \
    {{xo - 0.14f, -1.4f, -3.0f}, {xo + 0.14f, -0.6f, -0.8f}, kMetal},      /* trigger guard */      \
    {{xo - 0.1f, 1.4f, -7.6f}, {xo + 0.1f, 1.65f, -7.2f}, kMetal},         /* front sight */        \
    {{xo - 0.35f, 1.4f, 0.2f}, {xo + 0.35f, 1.65f, 0.7f}, kMetal}          /* rear sight */
const Part kBerettasBody[] = {BERETTA(0.0f), BERETTA(kLeftGun)};
const Part kBerettasArms[] = {
    {{-1.2f, -4.0f, -1.4f}, {1.3f, -1.0f, 1.6f}, kGlove},
    {{0.4f, -6.5f, 1.6f}, {3.2f, -3.5f, 11.0f}, kSleeve},
    {{kLeftGun - 1.3f, -4.0f, -1.4f}, {kLeftGun + 1.2f, -1.0f, 1.6f}, kGlove},
    {{kLeftGun - 3.2f, -6.5f, 1.6f}, {kLeftGun - 0.4f, -3.5f, 11.0f}, kSleeve},
};
const Part kBerettasMag[] = {
    {{-0.42f, -4.4f, -0.7f}, {0.42f, -0.7f, 0.9f}, kMetalLight},
    {{kLeftGun - 0.42f, -4.4f, -0.7f}, {kLeftGun + 0.42f, -0.7f, 0.9f}, kMetalLight},
};
#undef BERETTA

// Deagle: a big flat-sided slide with a rib on top and the hammer at the back, rubber grip.
const Part kDeagleBody[] = {
    {{-0.68f, 0.2f, -10.5f}, {0.68f, 1.9f, 1.2f}, kMetal},         // slide
    {{-0.36f, 1.9f, -10.2f}, {0.36f, 2.15f, 0.8f}, kMetalLight},   // top rib
    {{-0.42f, 0.5f, -11.0f}, {0.42f, 1.35f, -10.5f}, kMetalLight}, // muzzle
    {{-0.7f, 0.9f, -6.0f}, {0.7f, 1.2f, -1.0f}, kMetalLight},      // slide flat
    {{-0.56f, -0.7f, -8.5f}, {0.56f, 0.2f, 0.8f}, kMetal},         // frame
    {{-0.64f, -4.8f, -0.9f}, {0.64f, -0.7f, 1.7f}, kPolymer},      // grip
    {{-0.16f, -1.7f, -3.8f}, {0.16f, -0.7f, -0.6f}, kMetal},       // trigger guard
    {{-0.2f, 1.2f, 1.2f}, {0.2f, 2.0f, 1.8f}, kMetalLight},        // hammer
    {{-0.12f, 2.15f, -9.8f}, {0.12f, 2.45f, -9.3f}, kMetal},       // front sight
    {{-0.42f, 2.15f, 0.2f}, {0.42f, 2.45f, 0.8f}, kMetal},         // rear sight
};
const Part kDeagleArms[] = {
    {{-1.3f, -4.4f, -1.4f}, {1.4f, -1.0f, 1.8f}, kGlove},
    {{-1.7f, -4.6f, -2.0f}, {-0.45f, -1.2f, 1.4f}, kGlove},
    {{0.4f, -6.8f, 1.8f}, {3.3f, -3.6f, 11.0f}, kSleeve},
    {{-4.6f, -7.0f, 1.2f}, {-1.0f, -3.9f, 10.0f}, kSleeve},
};
const Part kDeagleMag[] = {
    {{-0.5f, -4.9f, -0.6f}, {0.5f, -0.7f, 1.4f}, kMetalLight},
};

// Nova: pump shotgun. The forend (and the left hand on it) slides back and forward after each shot.
const Part kNovaBody[] = {
    {{-0.85f, -1.0f, -4.0f}, {0.85f, 1.3f, 5.0f}, kMetal},          // receiver
    {{-0.42f, 0.2f, -26.0f}, {0.42f, 1.05f, -4.0f}, kMetal},        // barrel
    {{-0.38f, -0.75f, -22.0f}, {0.38f, 0.05f, -4.0f}, kMetalLight}, // magazine tube
    {{-0.12f, 1.05f, -25.6f}, {0.12f, 1.35f, -25.2f}, kMetalLight}, // front bead
    {{-0.3f, 1.3f, -2.0f}, {0.3f, 1.6f, 4.0f}, kMetalLight},        // top rail
    {{-0.75f, -1.8f, 5.0f}, {0.75f, 0.9f, 16.0f}, kPolymer},        // stock
    {{-0.75f, -3.4f, 10.0f}, {0.75f, -1.8f, 16.0f}, kPolymer},      // stock heel
    {{-0.65f, -3.6f, 4.0f}, {0.65f, -1.0f, 6.2f}, kPolymer},        // grip
    {{-0.2f, -2.0f, 0.8f}, {0.2f, -1.0f, 3.5f}, kMetal},            // trigger guard
};
const Part kNovaPump[] = {
    {{-0.78f, -1.3f, -17.0f}, {0.78f, 0.35f, -10.0f}, kPolymer},    // forend
    {{-0.8f, -1.35f, -16.0f}, {0.8f, -1.05f, -15.4f}, kMetal},      // grip ribs
    {{-0.8f, -1.35f, -13.0f}, {0.8f, -1.05f, -12.4f}, kMetal},
};
const Part kNovaArms[] = {
    {{-1.3f, -4.0f, 3.6f}, {1.4f, -1.2f, 6.6f}, kGlove},             // right hand
    {{0.6f, -6.5f, 6.6f}, {3.6f, -3.2f, 15.0f}, kSleeve},            // right forearm
};
const Part kNovaPumpArm[] = {
    {{-1.5f, -2.6f, -15.0f}, {1.5f, -0.8f, -11.5f}, kGlove},         // left hand on the pump
    {{-5.5f, -6.7f, -13.0f}, {-1.2f, -2.8f, -6.0f}, kSleeve},        // left forearm
};
const Part kNovaShell[] = {  // reloading: a red shell pushed up into the loading port
    {{-0.32f, -2.6f, 0.4f}, {0.32f, -1.9f, 2.2f}, 0xa8241e},
    {{-0.34f, -2.6f, 2.2f}, {0.34f, -1.9f, 2.6f}, 0xc9a13b},
};

// MAC-10: a boxy little SMG, the long mag in the grip, the wire stock folded along its sides.
const Part kMac10Body[] = {
    {{-0.9f, -1.3f, -5.5f}, {0.9f, 1.4f, 3.5f}, kMetal},            // body
    {{-0.4f, 1.4f, -5.0f}, {0.4f, 1.8f, 3.0f}, kMetalLight},        // top rail
    {{-0.3f, 0.0f, -8.5f}, {0.3f, 0.6f, -5.5f}, kMetal},            // barrel
    {{-0.38f, -0.1f, -9.2f}, {0.38f, 0.7f, -8.5f}, kMetalLight},    // muzzle
    {{-0.62f, -4.5f, -1.4f}, {0.62f, -1.3f, 0.8f}, kPolymer},       // grip
    {{-0.4f, -2.4f, -5.2f}, {0.4f, -1.3f, -4.2f}, kPolymer},        // hand stop
    {{-0.15f, -2.0f, -3.4f}, {0.15f, -1.3f, -1.6f}, kMetal},        // trigger guard
    {{0.95f, -0.3f, -4.0f}, {1.15f, 0.0f, 3.5f}, kMetalLight},      // folded stock rods
    {{-1.15f, -0.3f, -4.0f}, {-0.95f, 0.0f, 3.5f}, kMetalLight},
    {{-1.15f, -1.0f, 3.5f}, {1.15f, 0.4f, 3.9f}, kMetalLight},      // butt plate
    {{-0.25f, 1.8f, -4.6f}, {0.25f, 2.2f, -4.2f}, kMetal},          // sights
    {{-0.35f, 1.8f, 2.2f}, {0.35f, 2.2f, 2.8f}, kMetal},
};
const Part kMac10Arms[] = {
    {{-1.3f, -4.0f, -1.8f}, {1.4f, -1.2f, 1.2f}, kGlove},           // right hand on the grip
    {{0.4f, -6.5f, 1.2f}, {3.3f, -3.5f, 10.0f}, kSleeve},
    {{-1.45f, -3.2f, -5.8f}, {1.45f, -1.0f, -3.8f}, kGlove},        // left hand at the front
    {{-5.2f, -6.6f, -5.0f}, {-1.1f, -2.8f, 1.0f}, kSleeve},
};
const Part kMac10Mag[] = {
    {{-0.5f, -8.2f, -1.1f}, {0.5f, -4.5f, 0.5f}, kMetal},
    {{-0.55f, -8.5f, -1.2f}, {0.55f, -8.2f, 0.6f}, kMetalLight},
};

// ---- Knives. Blades are steel with a bright edge bevel and a darker fuller (a skin paints all three). ----

// Butterfly, built around the pivot pin at the origin: open, the blade points -z and both handles point
// back (+z). Each piece pivots on that pin, which is what makes the flips work.
const Part kButterflyBlade[] = {
    {{-0.09f, -0.45f, -7.6f}, {0.09f, 0.75f, -0.2f}, kBlade},
    {{-0.06f, -0.62f, -8.0f}, {0.06f, -0.45f, -0.6f}, kBladeEdge},   // edge bevel
    {{-0.11f, 0.15f, -6.0f}, {0.11f, 0.45f, -1.0f}, kBladeFuller},   // fuller
    {{-0.08f, -0.25f, -9.0f}, {0.08f, 0.65f, -7.6f}, kBlade},        // tip
    {{-0.06f, -0.05f, -9.8f}, {0.06f, 0.45f, -9.0f}, kBlade},
    {{-0.12f, -0.25f, -0.2f}, {0.12f, 0.35f, 0.25f}, kMetalLight},   // tang / pivot
};
const Part kButterflySafe[] = {  // the handle that stays in the hand: skeletonised, with pins
    {{-0.48f, -0.62f, 0.15f}, {-0.1f, 0.62f, 5.9f}, 0x2b2e34},
    {{-0.5f, -0.3f, 1.2f}, {-0.46f, 0.3f, 2.6f}, 0x111216},          // cut-outs
    {{-0.5f, -0.3f, 3.2f}, {-0.46f, 0.3f, 4.6f}, 0x111216},
    {{-0.5f, -0.2f, 0.4f}, {-0.08f, 0.2f, 0.9f}, kSilver},
    {{-0.5f, -0.2f, 5.2f}, {-0.08f, 0.2f, 5.7f}, kSilver},
};
const Part kButterflyBite[] = {  // the handle that swings round
    {{0.1f, -0.62f, 0.15f}, {0.48f, 0.62f, 5.9f}, 0x3c4048},
    {{0.46f, -0.3f, 1.2f}, {0.5f, 0.3f, 2.6f}, 0x15161a},
    {{0.46f, -0.3f, 3.2f}, {0.5f, 0.3f, 4.6f}, 0x15161a},
    {{0.08f, -0.28f, 5.9f}, {0.5f, 0.28f, 6.5f}, kSilver},  // latch
};
const Part kButterflyHand[] = {
    {{-1.25f, -1.4f, 1.6f}, {0.9f, 1.4f, 4.8f}, kGlove},  // hand around the safe handle
    {{-1.6f, -2.2f, 4.8f}, {1.6f, 1.6f, 14.0f}, kSleeve}, // forearm
};

const Part kGrenade[] = {
    {{-1.3f, -1.3f, -2.6f}, {1.3f, 1.3f, 2.4f}, 0x4f5a4a},      // canister
    {{-1.35f, -1.35f, -1.0f}, {1.35f, 1.35f, -0.4f}, 0x2a2e28}, // band
    {{-0.6f, -0.6f, -3.6f}, {0.6f, 0.6f, -2.6f}, kMetal},       // fuse
    {{-0.25f, 0.6f, -3.4f}, {0.25f, 1.6f, 1.2f}, kMetal},       // spoon
    {{-1.6f, -1.8f, -0.4f}, {1.6f, 1.2f, 3.0f}, kGlove},        // hand
    {{-1.6f, -2.2f, 3.0f}, {1.6f, 1.6f, 13.0f}, kSleeve},       // forearm
};

// Karambit / talon: a claw blade curving down from the handle, a finger ring at the back. The ring centre
// (0, -0.1, 5.4) is what they spin around.
const Part kKarambit[] = {
    {{-0.35f, -0.6f, 0.0f}, {0.35f, 0.6f, 4.5f}, 0x2d2a26},     // handle
    {{-0.37f, -0.3f, 0.8f}, {0.37f, 0.3f, 1.2f}, kSilver},      // pins
    {{-0.37f, -0.3f, 3.0f}, {0.37f, 0.3f, 3.4f}, kSilver},
    {{-0.3f, 0.3f, 4.5f}, {0.3f, 0.7f, 6.3f}, 0x2d2a26},        // ring
    {{-0.3f, -0.9f, 4.5f}, {0.3f, -0.5f, 6.3f}, 0x2d2a26},
    {{-0.3f, -0.9f, 5.9f}, {0.3f, 0.7f, 6.3f}, 0x2d2a26},
    {{-0.3f, -0.9f, 4.5f}, {0.3f, 0.7f, 4.9f}, kSilver},
    {{-0.08f, -0.3f, -2.2f}, {0.08f, 0.7f, 0.0f}, kBlade},      // the claw, curving down
    {{-0.08f, -1.0f, -3.8f}, {0.08f, 0.4f, -2.2f}, kBlade},
    {{-0.08f, -1.9f, -5.0f}, {0.08f, -0.3f, -3.8f}, kBlade},
    {{-0.07f, -2.6f, -5.8f}, {0.07f, -1.4f, -5.0f}, kBlade},
    {{-0.05f, -0.5f, -2.2f}, {0.05f, -0.3f, 0.0f}, kBladeEdge}, // edge on the inside of the curve
    {{-0.05f, -1.2f, -3.8f}, {0.05f, -1.0f, -2.2f}, kBladeEdge},
    {{-0.05f, -2.1f, -5.0f}, {0.05f, -1.9f, -3.8f}, kBladeEdge},
    {{-0.1f, 0.25f, -3.2f}, {0.1f, 0.45f, -0.4f}, kBladeFuller},// spine
};
const Part kTalon[] = {
    {{-0.4f, -0.65f, 0.0f}, {0.4f, 0.65f, 5.0f}, 0xd8cfb8},     // ivory handle
    {{-0.42f, -0.3f, 1.0f}, {0.42f, 0.3f, 1.4f}, 0xb8a46a},     // brass pins
    {{-0.42f, -0.3f, 3.4f}, {0.42f, 0.3f, 3.8f}, 0xb8a46a},
    {{-0.3f, 0.3f, 5.0f}, {0.3f, 0.75f, 7.0f}, 0xb8a46a},       // brass ring
    {{-0.3f, -1.0f, 5.0f}, {0.3f, -0.55f, 7.0f}, 0xb8a46a},
    {{-0.3f, -1.0f, 6.55f}, {0.3f, 0.75f, 7.0f}, 0xb8a46a},
    {{-0.09f, -0.3f, -2.6f}, {0.09f, 0.8f, 0.0f}, kBlade},      // long steel claw
    {{-0.09f, -1.1f, -4.6f}, {0.09f, 0.5f, -2.6f}, kBlade},
    {{-0.09f, -2.2f, -6.2f}, {0.09f, -0.4f, -4.6f}, kBlade},
    {{-0.08f, -3.1f, -7.2f}, {0.08f, -1.7f, -6.2f}, kBlade},
    {{-0.05f, -0.5f, -2.6f}, {0.05f, -0.3f, 0.0f}, kBladeEdge},
    {{-0.05f, -1.3f, -4.6f}, {0.05f, -1.1f, -2.6f}, kBladeEdge},
    {{-0.05f, -2.4f, -6.2f}, {0.05f, -2.2f, -4.6f}, kBladeEdge},
    {{-0.11f, 0.3f, -3.6f}, {0.11f, 0.55f, -0.5f}, kBladeFuller},
};
// Straight knives (held in kKnifeHand): handle back along +z, blade forward along -z.
const Part kDefaultKnife[] = {  // the plain black tactical knife everyone starts with (never skinned)
    {{-0.48f, -0.66f, 0.2f}, {0.48f, 0.66f, 5.0f}, 0x16171a},   // handle
    {{-0.5f, -0.68f, 1.4f}, {0.5f, 0.68f, 1.7f}, 0x24262a},     // grip ridges
    {{-0.5f, -0.68f, 2.6f}, {0.5f, 0.68f, 2.9f}, 0x24262a},
    {{-0.5f, -0.68f, 3.8f}, {0.5f, 0.68f, 4.1f}, 0x24262a},
    {{-0.5f, -0.55f, 5.0f}, {0.5f, 0.55f, 5.5f}, 0x2a2c30},     // pommel
    {{-0.52f, -0.9f, -0.2f}, {0.52f, 1.0f, 0.2f}, 0x2a2c30},    // guard
    {{-0.1f, -0.45f, -6.6f}, {0.1f, 0.7f, -0.2f}, 0x3d4046},    // coated blade
    {{-0.06f, -0.62f, -6.9f}, {0.06f, -0.45f, -0.5f}, 0x9aa0a8},// sharpened edge
    {{-0.08f, -0.25f, -7.8f}, {0.08f, 0.55f, -6.6f}, 0x3d4046}, // tip
    {{-0.06f, -0.05f, -8.4f}, {0.06f, 0.35f, -7.8f}, 0x3d4046},
};
const Part kM9[] = {
    {{-0.5f, -0.7f, 0.2f}, {0.5f, 0.7f, 5.2f}, 0x1e1f22},       // handle
    {{-0.52f, -0.72f, 1.2f}, {0.52f, 0.72f, 1.5f}, 0x2a2c30},   // ridges
    {{-0.52f, -0.72f, 2.4f}, {0.52f, 0.72f, 2.7f}, 0x2a2c30},
    {{-0.52f, -0.72f, 3.6f}, {0.52f, 0.72f, 3.9f}, 0x2a2c30},
    {{-0.55f, -0.6f, 5.2f}, {0.55f, 0.6f, 5.9f}, kSilver},      // pommel
    {{-0.6f, -1.3f, -0.3f}, {0.6f, 1.4f, 0.2f}, kSilver},       // guard
    {{-0.12f, -0.55f, -8.5f}, {0.12f, 0.8f, -0.3f}, kBlade},    // blade
    {{-0.07f, -0.75f, -8.8f}, {0.07f, -0.55f, -0.6f}, kBladeEdge},
    {{-0.14f, 0.1f, -6.5f}, {0.14f, 0.45f, -1.0f}, kBladeFuller},
    {{-0.1f, 0.8f, -6.0f}, {0.1f, 1.0f, -1.0f}, kBladeFuller},  // saw-back with teeth
    {{-0.1f, 1.0f, -5.6f}, {0.1f, 1.2f, -5.2f}, kBladeFuller},
    {{-0.1f, 1.0f, -4.4f}, {0.1f, 1.2f, -4.0f}, kBladeFuller},
    {{-0.1f, 1.0f, -3.2f}, {0.1f, 1.2f, -2.8f}, kBladeFuller},
    {{-0.1f, 1.0f, -2.0f}, {0.1f, 1.2f, -1.6f}, kBladeFuller},
    {{-0.1f, -0.4f, -10.0f}, {0.1f, 0.55f, -8.5f}, kBlade},     // tip
    {{-0.08f, -0.2f, -10.6f}, {0.08f, 0.25f, -10.0f}, kBlade},
};
const Part kBowie[] = {
    {{-0.55f, -0.75f, 0.6f}, {0.55f, 0.75f, 5.4f}, 0x6b4a2e},   // stag handle
    {{-0.57f, -0.77f, 0.3f}, {0.57f, 0.77f, 0.6f}, 0xb8923a},   // brass spacer
    {{-0.6f, -0.8f, 5.4f}, {0.6f, 0.8f, 6.2f}, 0xb8923a},       // pommel
    {{-0.65f, -1.5f, -0.35f}, {0.65f, 1.6f, 0.3f}, 0xc9a23e},   // brass guard
    {{-0.13f, -0.95f, -6.5f}, {0.13f, 0.9f, -0.35f}, kBlade},   // broad blade
    {{-0.07f, -1.15f, -9.4f}, {0.07f, -0.95f, -0.6f}, kBladeEdge},
    {{-0.15f, 0.0f, -5.0f}, {0.15f, 0.45f, -0.8f}, kBladeFuller},
    {{-0.12f, -0.95f, -9.0f}, {0.12f, 0.45f, -6.5f}, kBlade},   // the clip point
    {{-0.11f, -0.95f, -10.3f}, {0.11f, 0.0f, -9.0f}, kBlade},
    {{-0.09f, -0.85f, -11.2f}, {0.09f, -0.4f, -10.3f}, kBlade},
    {{-0.08f, 0.45f, -9.0f}, {0.08f, 0.62f, -6.8f}, kBladeEdge},// false edge on the clip
};
const Part kKukri[] = {
    {{-0.55f, -0.7f, 0.3f}, {0.55f, 0.7f, 5.0f}, 0x5a3820},     // wooden handle
    {{-0.6f, -1.0f, 5.0f}, {0.6f, 0.9f, 5.8f}, kSilver},        // flared pommel
    {{-0.6f, -0.85f, -0.2f}, {0.6f, 0.95f, 0.3f}, kSilver},     // bolster
    {{-0.13f, -0.6f, -2.5f}, {0.13f, 0.85f, -0.2f}, kBlade},    // the blade bends down, widening
    {{-0.13f, -1.1f, -4.8f}, {0.13f, 0.6f, -2.5f}, kBlade},
    {{-0.13f, -1.9f, -7.0f}, {0.13f, 0.1f, -4.8f}, kBlade},
    {{-0.13f, -2.7f, -8.8f}, {0.13f, -0.4f, -7.0f}, kBlade},
    {{-0.1f, -2.7f, -9.8f}, {0.1f, -1.3f, -8.8f}, kBlade},
    {{-0.07f, -0.8f, -2.5f}, {0.07f, -0.6f, -0.4f}, kBladeEdge},// edge along the inside of the bend
    {{-0.07f, -1.3f, -4.8f}, {0.07f, -1.1f, -2.5f}, kBladeEdge},
    {{-0.07f, -2.1f, -7.0f}, {0.07f, -1.9f, -4.8f}, kBladeEdge},
    {{-0.07f, -2.9f, -8.8f}, {0.07f, -2.7f, -7.0f}, kBladeEdge},
    {{-0.15f, 0.2f, -4.0f}, {0.15f, 0.5f, -0.6f}, kBladeFuller},
    {{-0.14f, -0.75f, -0.7f}, {0.14f, -0.5f, -0.4f}, 0x1a1a1a}, // the notch
};
const Part kKnifeHand[] = {
    {{-1.25f, -1.4f, 1.0f}, {1.25f, 1.4f, 4.3f}, kGlove},
    {{-1.6f, -2.2f, 4.3f}, {1.6f, 1.6f, 14.0f}, kSleeve},
};

// A part's box: painted by the skin (shaded by its role) when there is one, else its own colour.
BoxInstance partBox(const Part& p, bool gunPaint, bool bladePaint) {
    if (gunPaint) {
        const uint32_t c = p.color;
        if (c == kMetal) return makePainted(p.mins, p.maxs, 225);
        if (c == kMetalLight) return makePainted(p.mins, p.maxs, 255);
        if (c == kWood || c == kOlive) return makePainted(p.mins, p.maxs, 215);
        if (c == kWoodDark || c == kOliveDark) return makePainted(p.mins, p.maxs, 180);
        if (c == kPolymer) return makePainted(p.mins, p.maxs, 195);
    }
    if (bladePaint) {
        if (p.color == kBlade) return makePainted(p.mins, p.maxs, 230);
        if (p.color == kBladeEdge) return makePainted(p.mins, p.maxs, 255);
        if (p.color == kBladeFuller) return makePainted(p.mins, p.maxs, 165);
    }
    return makeBox(p.mins, p.maxs, p.color, false);
}

template <size_t N>
void addParts(std::vector<BoxInstance>& out, const Part (&parts)[N], bool gunPaint = false, bool bladePaint = false) {
    for (const Part& p : parts) out.push_back(partBox(p, gunPaint, bladePaint));
}
template <size_t N>
std::vector<BoxInstance> toBoxes(const Part (&parts)[N], bool gunPaint = false, bool bladePaint = false) {
    std::vector<BoxInstance> out;
    out.reserve(N);
    addParts(out, parts, gunPaint, bladePaint);
    return out;
}

// Inspect keyframes: offsets from the resting pose, eased between keys.
struct Pose { float t, x, y, z, yaw, pitch, roll; };
const Pose kRifleInspect[] = {
    {0.00f, 0, 0, 0, 0, 0, 0},
    {0.55f, -2.8f, 2.0f, 1.8f, 42, 6, -38},    // swing up to show the left side
    {1.35f, -2.4f, 2.2f, 1.6f, 34, 3, -30},    // ...and admire it
    {1.95f, -1.2f, 1.4f, 0.8f, -22, 12, 42},   // roll over to the right side
    {2.55f, -1.0f, 1.6f, 0.6f, -18, 9, 36},
    {3.05f, -0.6f, 0.4f, 0.4f, 4, -16, 10},    // tip down: look at the mag
    {3.60f, 0, 0, 0, 0, 0, 0},
};
const Pose kPistolInspect[] = {
    {0.00f, 0, 0, 0, 0, 0, 0},
    {0.45f, -2.0f, 1.8f, 1.4f, 40, 4, -34},
    {1.10f, -1.8f, 2.0f, 1.2f, 30, 2, -28},
    {1.60f, -1.0f, 1.2f, 0.6f, -24, 10, 44},
    {2.20f, -0.8f, 1.4f, 0.5f, -18, 8, 38},
    {2.60f, -0.4f, 0.3f, 0.2f, 2, -12, 8},
    {3.00f, 0, 0, 0, 0, 0, 0},
};

float smoother(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * x * (x * (x * 6 - 15) + 10);
}

template <size_t N>
Pose samplePose(const Pose (&keys)[N], float t) {
    if (t <= keys[0].t) return keys[0];
    for (size_t k = 1; k < N; ++k) {
        if (t > keys[k].t) continue;
        const Pose &a = keys[k - 1], &b = keys[k];
        float s = smoother((t - a.t) / (b.t - a.t));
        auto mix = [&](float p, float q) { return p + (q - p) * s; };
        return {t, mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.yaw, b.yaw), mix(a.pitch, b.pitch), mix(a.roll, b.roll)};
    }
    return keys[N - 1];
}

float smooth01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3 - 2 * x);
}
float ramp(float t, float a, float b) { return smooth01((t - a) / (b - a)); }

Mat4 cameraBasis(const Vec3& eye, float pitchDeg, float yawDeg) {
    Vec3 f = anglesToForward(pitchDeg, yawDeg), r = yawToRight(yawDeg), u = cross(r, f);
    return fromBasis(r, u, -f, eye);  // camera-local: x right, y up, z back
}

bool isPistolLike(ViewWeapon w) { return w == ViewWeapon::Pistol || w == ViewWeapon::Berettas || w == ViewWeapon::Deagle; }
bool isGun(ViewWeapon w) { return w != ViewWeapon::Knife && w != ViewWeapon::Grenade; }
bool ringKnife(int k) { return k == kKnifeKarambit || k == kKnifeTalon; }

// The skin's z extent for each model (fades and flames run front to back over it).
void paintSpan(ViewWeapon w, PaintParams& p) {
    switch (w) {
        case ViewWeapon::Pistol: p.zMin = -9.0f; p.zMax = 1.2f; break;
        case ViewWeapon::Berettas: p.zMin = -8.8f; p.zMax = 1.2f; break;
        case ViewWeapon::Deagle: p.zMin = -11.0f; p.zMax = 1.8f; break;
        case ViewWeapon::Sniper: p.zMin = -32.0f; p.zMax = 17.0f; break;
        case ViewWeapon::Nova: p.zMin = -26.0f; p.zMax = 16.0f; break;
        case ViewWeapon::Mac10: p.zMin = -9.2f; p.zMax = 3.9f; break;
        case ViewWeapon::Knife: p.zMin = -11.2f; p.zMax = 0.0f; break;
        default: p.zMin = -23.5f; p.zMax = 15.0f; break;
    }
}

// Animation state for one draw of a weapon (all zero for the showcase).
struct Anim {
    Vec3 magOffset;
    bool magVisible = true, arms = true;
    float bladeAngle = 0, biteAngle = 0, ringSpin = 0, pumpZ = 0;
    float shell = -1;  // Nova reload: 0..1 the shell going in, < 0 none
    int grenade = 0;
};

// The draws for weapon `w` (knife `knife`) in model space `world`, with its skin.
void weaponDraws(ViewWeapon w, int knife, const PaintParams& skinIn, const Mat4& world, const Anim& a,
                 std::vector<ModelDraw>& out) {
    PaintParams paint = skinIn;
    paintSpan(w, paint);
    const bool painted = paint.pattern >= 0;
    auto add = [&](const Mat4& m, std::vector<BoxInstance> boxes) { out.push_back({m, std::move(boxes), paint}); };
    switch (w) {
        case ViewWeapon::Rifle:
        case ViewWeapon::Sniper:
        case ViewWeapon::Pistol:
        case ViewWeapon::Deagle:
        case ViewWeapon::Berettas:
        case ViewWeapon::Mac10: {
            std::vector<BoxInstance> body, mag;
            if (w == ViewWeapon::Rifle) { addParts(body, kRifleBody, painted); if (a.arms) addParts(body, kRifleArms); addParts(mag, kRifleMag, painted); }
            if (w == ViewWeapon::Sniper) { addParts(body, kSniperBody, painted); if (a.arms) addParts(body, kSniperArms); addParts(mag, kSniperMag, painted); }
            if (w == ViewWeapon::Pistol) { addParts(body, kPistolBody, painted); if (a.arms) addParts(body, kPistolArms); addParts(mag, kPistolMag, painted); }
            if (w == ViewWeapon::Deagle) { addParts(body, kDeagleBody, painted); if (a.arms) addParts(body, kDeagleArms); addParts(mag, kDeagleMag, painted); }
            if (w == ViewWeapon::Berettas) {
                if (a.arms) {
                    addParts(body, kBerettasBody, painted);
                    addParts(body, kBerettasArms);
                    addParts(mag, kBerettasMag, painted);
                } else {  // the showcase: one of the pair (side on, the other would sit in front of it)
                    for (size_t k = 0; k < std::size(kBerettasBody) / 2; ++k) body.push_back(partBox(kBerettasBody[k], painted, false));
                    mag.push_back(partBox(kBerettasMag[0], painted, false));
                }
            }
            if (w == ViewWeapon::Mac10) { addParts(body, kMac10Body, painted); if (a.arms) addParts(body, kMac10Arms); addParts(mag, kMac10Mag, painted); }
            add(world, std::move(body));
            if (a.magVisible) add(world * translation(a.magOffset), std::move(mag));
            break;
        }
        case ViewWeapon::Nova: {
            std::vector<BoxInstance> body, pump;
            addParts(body, kNovaBody, painted);
            if (a.arms) addParts(body, kNovaArms);
            addParts(pump, kNovaPump, painted);
            if (a.arms) addParts(pump, kNovaPumpArm);
            add(world, std::move(body));
            add(world * translation({0, 0, a.pumpZ}), std::move(pump));
            if (a.shell >= 0) add(world * translation({0, 2.0f * a.shell, -1.5f * a.shell}), toBoxes(kNovaShell));
            break;
        }
        case ViewWeapon::Grenade: {
            // One body, tinted per type: smoke grey-green, flash pale grey, HE olive, molotov bottle brown.
            const uint32_t body[4] = {0x4f5a4a, 0xc9ccd0, 0x55602e, 0x7a4a1a};
            std::vector<BoxInstance> boxes = toBoxes(kGrenade);
            boxes[0] = makeBox(kGrenade[0].mins, kGrenade[0].maxs, body[std::clamp(a.grenade, 0, 3)], false);
            if (a.grenade == 3) {  // molotov: taller bottle with a burning rag
                boxes[0] = makeBox({-1.1f, -1.1f, -4.6f}, {1.1f, 1.1f, 2.4f}, body[3], false);
                boxes.push_back(makeEmissive({-0.5f, -0.5f, -6.2f}, {0.5f, 0.5f, -4.6f}, 0xffa030));
            }
            if (!a.arms) boxes.resize(boxes.size() - (a.grenade == 3 ? 3 : 2));
            add(world, std::move(boxes));
            break;
        }
        case ViewWeapon::Knife: {
            if (knife == kKnifeButterfly) {
                if (a.arms) add(world, toBoxes(kButterflyHand));
                add(world, toBoxes(kButterflySafe));
                add(world * rotationX(a.biteAngle), toBoxes(kButterflyBite));
                add(world * rotationX(a.bladeAngle), toBoxes(kButterflyBlade, false, painted));
            } else if (ringKnife(knife)) {
                const Vec3 ring{0, -0.1f, knife == kKnifeKarambit ? 5.4f : 5.8f};
                if (a.arms) add(world, toBoxes(kKnifeHand));
                add(world * translation(ring) * rotationX(a.ringSpin) * translation(-ring),
                    knife == kKnifeKarambit ? toBoxes(kKarambit, false, painted) : toBoxes(kTalon, false, painted));
            } else {
                if (a.arms) add(world, toBoxes(kKnifeHand));
                add(world, knife == kKnifeM9      ? toBoxes(kM9, false, painted)
                           : knife == kKnifeBowie ? toBoxes(kBowie, false, painted)
                           : knife == kKnifeKukri ? toBoxes(kKukri, false, painted)
                                                  : toBoxes(kDefaultKnife));
            }
            break;
        }
    }
}

}  // namespace

void ViewModel::onShot(uint32_t seed) {
    // The kick builds over a spray: the gun climbs and shakes harder the longer you hold it.
    shotsInRow_ = sinceShot_ < 0.25f ? shotsInRow_ + 1 : 1;
    sinceShot_ = 0;
    float build = std::min(float(shotsInRow_), 10.0f) / 10.0f;
    float r1 = float(seed % 1000) / 1000.0f - 0.5f, r2 = float((seed / 1000) % 1000) / 1000.0f - 0.5f;
    const float big = weapon_ == ViewWeapon::Deagle || weapon_ == ViewWeapon::Nova ? 1.6f : 1.0f;  // heavy hitters
    kickBack_ = std::min(kickBack_ + (2.0f + 0.6f * build) * big, 4.4f * big);
    kickPitch_ = std::min(kickPitch_ + (2.4f + 1.0f * build) * big, 7.5f * big);
    kickYaw_ += r1 * (0.7f + 0.8f * build);
    kickRoll_ += r2 * (2.0f + 3.0f * build);
    flashLeft_ = 0.045f;
    flashSeed_ = seed;
    inspectT_ = -1;
    ++shotIndex_;
}

void ViewModel::onLand(float fallSpeed) { landVel_ -= std::min(fallSpeed / 400.0f, 1.0f) * 28.0f; }

void ViewModel::onDraw(ViewWeapon w) {
    weapon_ = w;
    drawT_ = 0;
    inspectT_ = -1;
}

float ViewModel::inspectLength() const {
    switch (weapon_) {
        case ViewWeapon::Knife: return knife_ == kKnifeButterfly || ringKnife(knife_) ? 2.6f : 2.8f;
        case ViewWeapon::Grenade: return 1.4f;
        case ViewWeapon::Pistol:
        case ViewWeapon::Berettas:
        case ViewWeapon::Deagle: return 3.0f;
        default: return 3.6f;
    }
}

void ViewModel::inspect() {
    if (drawT_ >= 1.0f && reloadT_ < 0) inspectT_ = 0;
}

void ViewModel::update(const ViewModelInput& in) {
    float dt = std::min(in.dt, 0.05f);
    kickBack_ *= std::exp(-dt * 14.0f);
    kickPitch_ *= std::exp(-dt * 11.0f);
    kickYaw_ *= std::exp(-dt * 11.0f);
    kickRoll_ *= std::exp(-dt * 12.0f);
    sinceShot_ += dt;

    // Sway: the weapon lags slightly behind camera turns.
    swayYaw_ = std::clamp(swayYaw_ - in.mouseYawDelta * 0.5f, -3.0f, 3.0f) * std::exp(-dt * 10.0f);
    swayPitch_ = std::clamp(swayPitch_ - in.mousePitchDelta * 0.5f, -3.0f, 3.0f) * std::exp(-dt * 10.0f);

    // Bob scales with ground speed.
    float target = in.onGround ? std::min(in.horizSpeed / 250.0f, 1.0f) : 0.0f;
    bobAmount_ += (target - bobAmount_) * (1.0f - std::exp(-dt * 8.0f));
    bobPhase_ += dt * 2.0f * kPi * 1.6f * std::max(bobAmount_, 0.0f);

    // Landing dip: damped spring.
    float acc = -220.0f * landDip_ - 22.0f * landVel_;
    landVel_ += acc * dt;
    landDip_ += landVel_ * dt;

    // The butterfly flips open (slower draw); everything else just raises.
    drawT_ = std::min(1.0f, drawT_ + dt / (weapon_ == ViewWeapon::Knife ? 0.8f : 0.35f));
    if (inspectT_ >= 0) inspectT_ += dt;
    if (inspectT_ > inspectLength() || in.reloadProgress >= 0) inspectT_ = -1;
    flashLeft_ -= dt;
    primeT_ = std::clamp(primeT_ + (primed_ ? dt / 0.12f : -dt / 0.06f), 0.0f, 1.0f);  // wind up, snap out
    reloadT_ = in.reloadProgress;
    reloadTime_ = in.reloadTime;
}

void ViewModel::build(const Vec3& eye, float pitchDeg, float yawDeg, float offX, float offY, float offZ,
                      float bobScale, std::vector<ModelDraw>& out) const {
    const bool sniper = weapon_ == ViewWeapon::Sniper;
    const bool pistol = isPistolLike(weapon_), mac = weapon_ == ViewWeapon::Mac10;
    const bool rifle = isGun(weapon_) && !pistol && !mac;  // rifle, sniper, nova
    const bool gun = isGun(weapon_);
    // Camera-local placement (x right, y up, z back). Tuned so the guns sit lower-right like CS; the
    // Berettas sit in the middle, one in each hand.
    Vec3 pos = weapon_ == ViewWeapon::Berettas ? Vec3{4.6f, -6.2f, -16.0f}
               : rifle                         ? Vec3{10.9f, -6.9f, -22.0f}
               : mac                           ? Vec3{8.6f, -6.4f, -17.5f}
               : pistol                        ? Vec3{7.5f, -6.0f, -16.0f}
                                               : Vec3{9.5f, -7.5f, -18.0f};
    pos += Vec3{offX, offY, -offZ};
    const float modelScale = rifle ? 0.75f : 0.85f;
    float pitch = gun ? 2.0f : 10.0f, yaw = rifle ? 4.0f : pistol || mac ? 3.0f : 6.0f, roll = gun ? 0.0f : -15.0f;
    if (weapon_ == ViewWeapon::Berettas) yaw = 0.0f;

    // Walk bob (figure-eight) and landing dip.
    pos.x += std::cos(bobPhase_) * 0.32f * bobAmount_ * bobScale;
    pos.y += std::sin(2.0f * bobPhase_) * 0.16f * bobAmount_ * bobScale + landDip_ * 0.08f;

    // Grenade wind-up while the pin's out: back and up for a throw, down low for an underhand lob.
    if (weapon_ == ViewWeapon::Grenade && primeT_ > 0) {
        const float k = smooth01(primeT_);
        if (primedPose_ == 2) {
            pos.y -= 3.5f * k;
            pitch += 18.0f * k;
        } else {
            pos.z += (primedPose_ == 3 ? 2.5f : 4.0f) * k;
            pos.y += 2.5f * k;
            pitch -= 14.0f * k;
        }
    }
    // Recoil kick.
    pos.z += kickBack_;
    pitch += kickPitch_;
    roll += kickRoll_;
    yaw += kickYaw_ + swayYaw_;
    pitch += swayPitch_;

    // Draw (raise from below).
    float e = 1.0f - smooth01(weapon_ == ViewWeapon::Knife ? drawT_ * 3.0f : drawT_);
    pos.y -= 7.0f * e;
    pitch -= 30.0f * e;

    Anim a;
    a.grenade = grenade_;
    // The Nova's pump: back and forward again after each shot.
    if (weapon_ == ViewWeapon::Nova && sinceShot_ < 0.7f)
        a.pumpZ = 4.0f * (ramp(sinceShot_, 0.16f, 0.32f) - ramp(sinceShot_, 0.4f, 0.58f));
    // Reload. The Nova: tilted, a shell pushed up into the port each time. Mags: tilt, drop the old one,
    // insert a new one, rack it.
    if (weapon_ == ViewWeapon::Nova && reloadT_ >= 0) {
        const float k = std::clamp(reloadT_ / std::max(reloadTime_, 0.05f), 0.0f, 1.0f);
        roll -= 18.0f;
        pitch += 6.0f;
        a.shell = k;
    } else if (gun && reloadT_ >= 0) {
        float t = reloadT_, T = reloadTime_;
        float env = ramp(t, 0.0f, 0.35f) * (1.0f - ramp(t, T - 0.45f, T - 0.1f));
        roll -= 28.0f * env;
        pitch += 9.0f * env;
        pos.x -= 1.0f * env;
        pos.y += 0.6f * env;
        if (t < 0.3f) {
        } else if (t < 0.75f) {
            a.magOffset.y = -14.0f * ramp(t, 0.3f, 0.75f);
        } else if (t < 1.0f) {
            a.magVisible = false;
        } else if (t < 1.4f) {
            a.magOffset.y = -10.0f * (1.0f - ramp(t, 1.0f, 1.4f));
        }
        float bolt = ramp(t, 1.95f, 2.05f) * (1.0f - ramp(t, 2.1f, 2.25f));
        pos.z += 1.2f * bolt;
    }

    // Inspect (F). Guns follow keyframes: left side, roll over to the right side, look at the mag.
    // A breathing sway rides on top so the hold never looks frozen. Smoke: a toss and catch. Knives:
    // the butterfly does aerials, the karambit and talon spin round the finger ring, the others are tossed.
    if (inspectT_ >= 0) {
        const float t = inspectT_;
        if (gun) {
            Pose p = pistol ? samplePose(kPistolInspect, t) : samplePose(kRifleInspect, t);
            float hold = ramp(t, 0.3f, 0.8f) * (1.0f - ramp(t, inspectLength() - 0.6f, inspectLength()));
            pos += Vec3{p.x, p.y + 0.15f * std::sin(t * 3.1f) * hold, p.z};
            yaw += p.yaw + 2.0f * std::sin(t * 1.7f) * hold;
            pitch += p.pitch + 1.5f * std::sin(t * 2.3f + 1.0f) * hold;
            roll += p.roll;
        } else if (weapon_ == ViewWeapon::Grenade) {
            float up = std::sin(kPi * std::clamp((t - 0.25f) / 0.8f, 0.0f, 1.0f));
            pos.y += 5.0f * up;
            roll += 360.0f * ramp(t, 0.25f, 1.05f);
            pitch += 20.0f * up;
        } else {
            float in = ramp(t, 0.0f, 0.35f) * (1.0f - ramp(t, inspectLength() - 0.4f, inspectLength()));
            pos += Vec3{-3.0f * in, 2.0f * in, 2.0f * in};
            float look = ramp(t, 1.6f, 1.9f) * (1.0f - ramp(t, 2.1f, 2.4f));
            if (knife_ == kKnifeButterfly) {  // two aerials, then a look at the blade
                roll += 15.0f * in + 360.0f * ramp(t, 0.4f, 1.6f);
                a.biteAngle = 720.0f * ramp(t, 0.4f, 1.6f);
                a.bladeAngle = -720.0f * ramp(t, 0.45f, 1.55f);
                yaw += 70.0f * look;
                pitch -= 10.0f * look;
            } else if (ringKnife(knife_)) {  // spins round the finger, then shows the claw
                a.ringSpin = (knife_ == kKnifeKarambit ? 720.0f : 540.0f) * ramp(t, 0.35f, 1.45f);
                roll += 10.0f * in;
                yaw += 60.0f * look;
                roll -= 25.0f * look;
            } else {  // straight knives: tossed up end over end, caught, then turned to the light
                float air = std::sin(kPi * std::clamp((t - 0.4f) / 0.8f, 0.0f, 1.0f));
                pos.y += 7.0f * air;
                pitch += 360.0f * ramp(t, 0.4f, 1.2f);
                float turn = ramp(t, 1.4f, 1.8f) * (1.0f - ramp(t, 2.3f, 2.7f));
                yaw -= 55.0f * turn;
                roll += 35.0f * turn;
            }
        }
    }
    if (weapon_ == ViewWeapon::Knife && drawT_ < 1.0f) {
        if (knife_ == kKnifeButterfly) {
            // Flip open: the bite handle fans a full turn while the blade spins out of the handles.
            a.biteAngle += 360.0f * ramp(drawT_, 0.1f, 0.85f);
            a.bladeAngle += 180.0f - 540.0f * ramp(drawT_, 0.15f, 0.9f);
        } else if (ringKnife(knife_)) {
            a.ringSpin += 360.0f * ramp(drawT_, 0.1f, 0.85f);
        } else {
            roll += 360.0f * (1.0f - ramp(drawT_, 0.1f, 0.85f));
        }
    }

    Mat4 world = cameraBasis(eye, pitchDeg, yawDeg) * translation(pos) * rotationY(yaw) * rotationX(pitch) *
                 rotationZ(roll) * scaling(modelScale);
    weaponDraws(weapon_, knife_, skins_[int(weapon_)], world, a, out);

    if (gun && flashLeft_ > 0) {
        float spin = float(flashSeed_ % 90);
        float s = (1.0f + float((flashSeed_ / 90) % 50) / 100.0f) * (rifle ? 1.0f : 0.75f) *
                  (weapon_ == ViewWeapon::Nova || weapon_ == ViewWeapon::Deagle ? 1.35f : 1.0f);
        Vec3 muzzle = sniper                           ? Vec3{0, 0.2f, -33.3f}
                      : weapon_ == ViewWeapon::Nova     ? Vec3{0, 0.6f, -26.8f}
                      : weapon_ == ViewWeapon::Rifle    ? Vec3{0, 0.2f, -24.8f}
                      : weapon_ == ViewWeapon::Deagle   ? Vec3{0, 0.9f, -11.8f}
                      : weapon_ == ViewWeapon::Mac10    ? Vec3{0, 0.3f, -9.9f}
                      : weapon_ == ViewWeapon::Berettas ? Vec3{(shotIndex_ & 1) ? kLeftGun : 0.0f, 0.6f, -9.5f}
                                                        : Vec3{0, 0.8f, -10.0f};
        ModelDraw flash{world * translation(muzzle) * rotationZ(spin), {}, {}};
        flash.boxes.push_back(makeEmissive({-0.9f * s, -0.9f * s, -1.6f}, {0.9f * s, 0.9f * s, 1.0f}, 0xfff4c0));
        flash.boxes.push_back(makeEmissive({-2.8f * s, -0.22f, -0.6f}, {2.8f * s, 0.22f, 0.6f}, 0xffc24a));
        flash.boxes.push_back(makeEmissive({-0.22f, -2.8f * s, -0.6f}, {0.22f, 2.8f * s, 0.6f}, 0xffc24a));
        flash.boxes.push_back(makeEmissive({-0.5f, -0.5f, -4.0f * s}, {0.5f, 0.5f, -1.0f}, 0xffe08a));
        out.push_back(flash);
    }
}

void ViewModel::buildShowcase(ViewWeapon w, int knife, const PaintParams& paint, const Vec3& eye, float pitchDeg,
                              float yawDeg, float spinDeg, const Vec3& centre, float size, std::vector<ModelDraw>& out) {
    // Side on, turning slowly, scaled so its length is `size`; centred on the middle of the model.
    PaintParams span;
    paintSpan(w, span);
    const float length = w == ViewWeapon::Knife ? 17.0f : span.zMax - span.zMin;
    const float mid = w == ViewWeapon::Knife ? -2.5f : (span.zMax + span.zMin) * 0.5f;
    Mat4 world = cameraBasis(eye, pitchDeg, yawDeg) * translation(centre) * rotationY(90.0f + spinDeg) *
                 rotationX(w == ViewWeapon::Knife ? 0.0f : -4.0f) * scaling(size / length) * translation({0, 0, -mid});
    Anim a;
    a.arms = false;
    weaponDraws(w, knife, paint, world, a, out);
}

Vec3 ViewModel::muzzleWorld(const Vec3& eye, float pitchDeg, float yawDeg) const {
    // Chosen so the tracer starts where the muzzle appears on screen (the weapon uses its own FOV).
    return transformPoint(cameraBasis(eye, pitchDeg, yawDeg), {14.4f, -9.3f, -40.0f});
}

float Effects::rnd() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return float(rng_) / 2147483648.0f - 1.0f;
}

void Effects::impact(const Vec3& pos, const Vec3& normal, uint32_t color, float scale) {
    for (int i = 0; i < 6; ++i) {
        Vec3 v = normal * (90.0f + 120.0f * (rnd() * 0.5f + 0.5f)) + Vec3{rnd(), rnd(), rnd() + 0.6f} * 90.0f;
        particles_.push_back({pos + normal * 0.5f, v * std::sqrt(scale), 0, 0.35f + 0.25f * (rnd() * 0.5f + 0.5f),
                              (0.5f + 0.4f * (rnd() * 0.5f + 0.5f)) * scale, color});
    }
    for (int i = 0; i < 4; ++i) {  // dust puff (bigger far away so a spray's landing spot stays readable)
        Vec3 v = normal * 35.0f + Vec3{rnd(), rnd(), rnd()} * 15.0f + Vec3{0, 0, 260.0f};
        particles_.push_back({pos + normal * 1.0f, v, 0, 0.26f, (1.8f + rnd() * 0.6f) * scale, 0xb9b2a3});
    }
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::burst(const Vec3& pos, uint32_t color, float scale) {
    for (int i = 0; i < 10; ++i) {  // fireball: big glowing blobs that shrink fast
        Vec3 v = Vec3{rnd(), rnd(), rnd() * 0.5f + 0.6f} * (90.0f * scale);
        particles_.push_back({pos + Vec3{0, 0, 16}, v, 0, 0.25f, 26.0f * scale, i % 2 ? color : 0xfff0c0, true});
    }
    for (int i = 0; i < 30; ++i) {  // hot sparks
        Vec3 v = Vec3{rnd(), rnd(), rnd() * 0.5f + 0.7f} * (300.0f * scale);
        particles_.push_back({pos + Vec3{0, 0, 6}, v, 0, 0.45f + 0.3f * (rnd() * 0.5f + 0.5f), 4.0f * scale, color, true});
    }
    for (int i = 0; i < 12; ++i) {  // smoke left behind
        Vec3 v = Vec3{rnd() * 60.0f, rnd() * 60.0f, 120.0f + rnd() * 40.0f} * scale;
        particles_.push_back({pos + Vec3{0, 0, 10}, v, 0, 1.4f, 18.0f * scale, 0x6b6f75});
    }
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::shell(const Vec3& pos, const Vec3& vel) {
    particles_.push_back({pos, vel, 0, 1.4f, 0.32f, 0xc9a13b});
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 100);
}

void Effects::blood(const Vec3& pos, const Vec3& dir) {
    for (int i = 0; i < 9; ++i) {
        Vec3 v = dir * 60.0f + Vec3{rnd(), rnd(), rnd() + 0.8f} * 70.0f;
        uint32_t c = i % 2 ? 0x8a1010 : 0xb01c1c;
        particles_.push_back({pos, v, 0, 0.3f + 0.2f * (rnd() * 0.5f + 0.5f), 0.6f + 0.6f * (rnd() * 0.5f + 0.5f), c});
    }
}

void Effects::tracer(const Vec3& from, const Vec3& to) {
    Vec3 d = to - from;
    float len = length(d);
    if (len < 64.0f) return;
    tracers_.push_back({from, d * (1.0f / len), len, 0});
}

void Effects::update(float dt) {
    dt = std::min(dt, 0.05f);
    for (Particle& p : particles_) {
        p.life += dt;
        p.vel.z -= 800.0f * dt;
        p.pos += p.vel * dt;
        float floorZ = ground_ ? ground_(p.pos.x, p.pos.y) : 0.0f;
        if (p.pos.z < floorZ + 0.3f && p.pos.z > floorZ - 48.0f) { p.pos.z = floorZ + 0.3f; p.vel = {}; }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life >= p.maxLife; }),
                     particles_.end());
    for (Tracer& t : tracers_) t.travelled += 9000.0f * dt;
    tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(),
                                  [](const Tracer& t) { return t.travelled - 160.0f > t.length; }),
                   tracers_.end());
}

void Effects::appendParticles(std::vector<BoxInstance>& out) const {
    for (const Particle& p : particles_) {
        float h = p.size * 0.5f * std::sqrt(1.0f - p.life / p.maxLife);
        out.push_back(p.glow ? makeEmissive(p.pos - Vec3{h, h, h}, p.pos + Vec3{h, h, h}, p.color)
                             : makeBox(p.pos - Vec3{h, h, h}, p.pos + Vec3{h, h, h}, p.color, false));
    }
}

void Effects::appendTracers(std::vector<ModelDraw>& out) const {
    for (const Tracer& t : tracers_) {
        float head = std::min(t.travelled, t.length), tail = std::max(0.0f, t.travelled - 160.0f);
        if (head <= tail) continue;
        Vec3 up = std::fabs(t.dir.z) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        Vec3 r = normalize(cross(t.dir, up)), u = cross(t.dir, r);
        Mat4 m = fromBasis(r, u, t.dir, t.start + t.dir * tail);
        out.push_back({m, {makeEmissive({-0.16f, -0.16f, 0}, {0.16f, 0.16f, head - tail}, 0xffe39a)}});
    }
}
