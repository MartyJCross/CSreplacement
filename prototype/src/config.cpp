#include "config.h"
#include <fstream>
#include <sstream>

namespace {

const char* kDefaultConfig = R"(// Feel Lab config. Edit and restart the game.
// Sensitivity uses CS's m_yaw 0.022, so your CS sensitivity feels identical (same DPI).
sensitivity 2.0
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
)";

}  // namespace

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
    }
    return c;
}
