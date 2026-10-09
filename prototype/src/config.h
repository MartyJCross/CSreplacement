// Plain-text config ("key value" per line, // or # comments), CS-style names where they exist.
#pragma once
#include <string>

struct Config {
    float sensitivity = 1.2f;
    float m_yaw = 0.022f;     // degrees per mouse count, same as CS: your CS sensitivity carries over
    float m_pitch = 0.022f;
    float fov = 90.0f;        // horizontal FOV at 4:3, like CS (wider screens get more, not less)
    int fps_max = 0;          // 0 = unlimited
    int vsync = 0;
    int low_latency = 1;      // 1 = never more than one frame queued on the GPU (less input lag when GPU-bound)
    int fullscreen = 1;
    int width = 0, height = 0;  // 0 = desktop resolution (fullscreen) / 1280x720 (windowed)
    int crosshair_size = 5;
    int crosshair_gap = 3;
    int crosshair_thickness = 2;
    int crosshair_r = 0, crosshair_g = 255, crosshair_b = 0;
    int crosshair_outline = 1;
    int crosshair_dot = 0;
    float view_recoil_tracking = 0.45f;  // fraction of recoil that moves the camera
    int hud_scale = 0;                   // 0 = auto
    float volume = 0.6f;
    float viewmodel_fov = 68.0f;         // like CS: horizontal FOV at 4:3 for the weapon pass
    float viewmodel_offset_x = 0, viewmodel_offset_y = 0, viewmodel_offset_z = 0;
    float viewmodel_bob = 1.0f;          // 0 disables weapon bob
    int show_viewmodel = 1;
    int spread_spray = 0;      // 1 = random spread grows during sprays (CS-like)
    int spread_movement = 0;   // 1 = random spread while moving/airborne (CS-like)
    int bhop = 0;              // 1 = hold jump to auto-hop, no stamina penalty; 0 = CS-style (the default)
    int camera_extrapolate = 1;  // 1 = zero-lag camera (render your position at "now", not 1 tick behind)
    int view_shake = 1;          // 1 = slight camera roll per shot (around the crosshair: aim is unaffected)
    int case_kills = 25;         // kills (deathmatch, retakes, competitive, online) for each case
    int all_skins = 0;           // 1 = every skin to pick from without opening cases (testing)
    int radar = 1;               // 1 = radar top-left on Dust
    int hitmarker = 1;         // 1 = X on the crosshair when you hit (red = head, big = kill)
    int hitsound = 1;            // 1 = a tick sound when you hit
    int view_smooth_steps = 1;   // 1 = the camera eases over stairs/steps instead of popping up (cosmetic)
    float zoom_sensitivity_ratio = 1.0f;  // like CS: 1 = same feel scoped as unscoped
    int map = 0;               // 0 = the Lab, 1 = dust (Play screen)
    int dust_scale = 60;       // Dust's size in % of real Dust2 (50..100)
    int msaa = 4;              // anti-aliasing samples (0 = off); applies on restart
    int depth_prepass = 1;     // 1 = depth pre-pass (faster on most GPUs, identical image)
    int mode = 0;              // 0 practice, 1 deathmatch, 2 retakes, 3 competitive, 4 prefire (Play screen)
    int dm_bots = 10;          // deathmatch: number of bots
    int dm_minutes = 5;        // deathmatch: match length
    int rt_bots = 4;           // retakes: bots holding the site (1..6)
    int comp_mates = 4;        // competitive: bots on your team (0..4)
    int comp_enemies = 5;      // competitive: bots on the other team (1..5)
    int freeze_time = 15;      // competitive: seconds of freeze time (buying) before each round
    int mate_skill = 1;        // competitive teammates' skill: 0 easy, 1 normal, 2 hard, 3 expert
    int enemy_skill = 2;       // enemy bots' skill, every mode (owner: normal was too easy)
    int nade_preview = 1;      // grenade trajectory preview: 0 off, 1 everywhere but competitive, 2 always
    std::string net_address = "127.0.0.1";  // online: the host to join (IP or name, optionally :port)
    std::string player_name = "PLAYER";      // online: your name
    int net_port = 27015;      // online: UDP port (host: open/forward this one)
    int net_game = 0;          // online, hosting: 0 deathmatch, 1 competitive
    int net_teams = 0;         // online competitive: 0 players split between the teams, 1 all on one team vs bots
    int prefire_bots_shoot = 1;  // prefire: the bots shoot back
};

// Loads `path`; writes a commented default file there if it doesn't exist.
Config loadConfig(const std::string& path);
// Writes every setting back to `path` (used by the in-game settings menu).
bool saveConfig(const std::string& path, const Config& c);
