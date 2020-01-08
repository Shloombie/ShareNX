#pragma once
#include <pu/ui/ui_Types.hpp>
#include <json.hpp>

extern Theme g_Theme;

class Theme {
public:
    Theme(const nlohmann::json& json);
    struct {
        pu::ui::Color background;
        pu::ui::Color focus;
        pu::ui::Color text;
        pu::ui::Color topbar;
    } color;
    std::string background_path;
    struct {
        std::string path;
        u32 x, y, w, h;
    } image;
};
