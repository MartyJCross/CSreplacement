// Plain-text config ("key value" per line, // or # comments), CS-style names where they exist.
#pragma once
#include <string>

struct Config {
    float sensitivity = 2.0f;
    float m_yaw = 0.022f;     // degrees per mouse count, same as CS: your CS sensitivity carries over
    float m_pitch = 0.022f;
    float fov = 90.0f;        // horizontal FOV at 4:3, like CS (wider screens get more, not less)
    int fps_max = 0;          // 0 = unlimited
    int vsync = 0;
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
};

// Loads `path`; writes a commented default file there if it doesn't exist.
Config loadConfig(const std::string& path);
