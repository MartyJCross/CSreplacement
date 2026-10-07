#include "config.h"
#include <fstream>
#include <sstream>

namespace {

const char* kDefaultConfig = R"(// Feel Lab config. Edit and restart the game.
// Sensitivity uses CS's m_yaw 0.022, so your CS sensitivity feels identical (same DPI).
sensitivity 1.2
m_yaw 0.022
m_pitch 0.022
// Horizontal FOV at 4:3, same as CS's 90.
fov 90
// 0 = unlimited
fps_max 0
vsync 0
fullscreen 1
// 0 = desktop resolution (try e.g. 1280 960 for 4:3 stretched)
width 0
height 0
crosshair_size 5
crosshair_gap 3
crosshair_thickness 2
crosshair_r 0
crosshair_g 255
crosshair_b 0
crosshair_outline 1
crosshair_dot 0
// How much of the recoil moves your view (CS uses ~0.45).
view_recoil_tracking 0.45
// 0 = auto
hud_scale 0
// Master volume 0..1
volume 0.6
// First-person weapon: FOV (like CS viewmodel_fov), position offsets, bob strength, visibility.
viewmodel_fov 68
viewmodel_offset_x 0
viewmodel_offset_y 0
viewmodel_offset_z 0
viewmodel_bob 1
show_viewmodel 1
// Random bullet spread is OFF: bullets go exactly to crosshair + the fixed recoil pattern.
// Set to 1 for CS-like random spread during sprays / while moving.
spread_spray 0
spread_movement 0
// 1 = bunny hopping: hold jump (or spam the wheel) to hop on landing, no landing slowdown.
bhop 1
// 1 = zero-lag camera: your view is drawn where you are *now*, not up to one tick behind.
camera_extrapolate 1
// 1 = the camera eases over stairs and steps instead of popping up (cosmetic; movement is unchanged).
view_smooth_steps 1
// Scoped sensitivity multiplier (1 = matched to unscoped, like CS).
zoom_sensitivity_ratio 1
// Anti-aliasing samples (0 = off, 2, 4, 8). Smooths edges so far-away players are easier to see. Restart to apply.
msaa 4
// Deathmatch on Dust (F7): number of bots and match length in minutes.
dm_bots 6
dm_minutes 5
)";

}  // namespace

bool saveConfig(const std::string& path, const Config& c) {
    std::ofstream out(path);
    if (!out) return false;
    out << "// Feel Lab config. Written by the in-game settings menu (Esc); you can also edit it by hand.\n";
    out << "sensitivity " << c.sensitivity << "\nm_yaw " << c.m_yaw << "\nm_pitch " << c.m_pitch
        << "\nzoom_sensitivity_ratio " << c.zoom_sensitivity_ratio << "\nfov " << c.fov << "\nfps_max " << c.fps_max
        << "\nvsync " << c.vsync << "\nfullscreen " << c.fullscreen << "\nwidth " << c.width << "\nheight " << c.height
        << "\ncrosshair_size " << c.crosshair_size << "\ncrosshair_gap " << c.crosshair_gap
        << "\ncrosshair_thickness " << c.crosshair_thickness << "\ncrosshair_r " << c.crosshair_r
        << "\ncrosshair_g " << c.crosshair_g << "\ncrosshair_b " << c.crosshair_b
        << "\ncrosshair_outline " << c.crosshair_outline << "\ncrosshair_dot " << c.crosshair_dot
        << "\nview_recoil_tracking " << c.view_recoil_tracking << "\nhud_scale " << c.hud_scale
        << "\nvolume " << c.volume << "\nviewmodel_fov " << c.viewmodel_fov
        << "\nviewmodel_offset_x " << c.viewmodel_offset_x << "\nviewmodel_offset_y " << c.viewmodel_offset_y
        << "\nviewmodel_offset_z " << c.viewmodel_offset_z << "\nviewmodel_bob " << c.viewmodel_bob
        << "\nshow_viewmodel " << c.show_viewmodel << "\nspread_spray " << c.spread_spray
        << "\nspread_movement " << c.spread_movement << "\nbhop " << c.bhop
        << "\ncamera_extrapolate " << c.camera_extrapolate << "\nview_smooth_steps " << c.view_smooth_steps
        << "\nmap " << c.map << "\nmsaa " << c.msaa << "\nmode " << c.mode << "\ndm_bots " << c.dm_bots
        << "\ndm_minutes " << c.dm_minutes << "\n";
    return bool(out);
}

Config loadConfig(const std::string& path) {
    Config c;
    std::ifstream in(path);
    if (!in) {
        std::ofstream out(path);
        if (out) out << kDefaultConfig;
        return c;
    }
    std::string line;
    while (std::getline(in, line)) {
        size_t cut = line.find("//");
        if (cut != std::string::npos) line.resize(cut);
        cut = line.find('#');
        if (cut != std::string::npos) line.resize(cut);
        std::istringstream ss(line);
        std::string key;
        float v;
        if (!(ss >> key >> v)) continue;
        auto i = [&](int& dst) { dst = int(v); };
        if (key == "sensitivity") c.sensitivity = v;
        else if (key == "m_yaw") c.m_yaw = v;
        else if (key == "m_pitch") c.m_pitch = v;
        else if (key == "fov") c.fov = v;
        else if (key == "fps_max") i(c.fps_max);
        else if (key == "vsync") i(c.vsync);
        else if (key == "fullscreen") i(c.fullscreen);
        else if (key == "width") i(c.width);
        else if (key == "height") i(c.height);
        else if (key == "crosshair_size") i(c.crosshair_size);
        else if (key == "crosshair_gap") i(c.crosshair_gap);
        else if (key == "crosshair_thickness") i(c.crosshair_thickness);
        else if (key == "crosshair_r") i(c.crosshair_r);
        else if (key == "crosshair_g") i(c.crosshair_g);
        else if (key == "crosshair_b") i(c.crosshair_b);
        else if (key == "crosshair_outline") i(c.crosshair_outline);
        else if (key == "crosshair_dot") i(c.crosshair_dot);
        else if (key == "view_recoil_tracking") c.view_recoil_tracking = v;
        else if (key == "hud_scale") i(c.hud_scale);
        else if (key == "volume") c.volume = v;
        else if (key == "viewmodel_fov") c.viewmodel_fov = v;
        else if (key == "viewmodel_offset_x") c.viewmodel_offset_x = v;
        else if (key == "viewmodel_offset_y") c.viewmodel_offset_y = v;
        else if (key == "viewmodel_offset_z") c.viewmodel_offset_z = v;
        else if (key == "viewmodel_bob") c.viewmodel_bob = v;
        else if (key == "show_viewmodel") i(c.show_viewmodel);
        else if (key == "spread_spray") i(c.spread_spray);
        else if (key == "spread_movement") i(c.spread_movement);
        else if (key == "bhop") i(c.bhop);
        else if (key == "camera_extrapolate") i(c.camera_extrapolate);
        else if (key == "view_smooth_steps") i(c.view_smooth_steps);
        else if (key == "zoom_sensitivity_ratio") c.zoom_sensitivity_ratio = v;
        else if (key == "map") i(c.map);
        else if (key == "msaa") i(c.msaa);
        else if (key == "mode") i(c.mode);
        else if (key == "dm_bots") i(c.dm_bots);
        else if (key == "dm_minutes") i(c.dm_minutes);
    }
    return c;
}
