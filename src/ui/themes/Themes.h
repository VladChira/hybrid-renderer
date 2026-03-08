#pragma once

#include "imgui.h"

#include "EmbraceTheDarkness.h"

#include "EmbraceTheLightness.h"

namespace hybrid::ui
{

    enum class ThemeKind
    {
        Darkness,
        Lightness
    };

    inline std::string ThemeKindToString(ThemeKind theme_kind)
    {
        switch(theme_kind)
        {
            case ThemeKind::Darkness:
                return "Darkness";
            case ThemeKind::Lightness:
                return "Lightness";
        }
        return "unknown"; // this is fine, no need to throw or assert here
    }

    struct LogColorPalette
    {
        ImVec4 critical{};
        ImVec4 error{};
        ImVec4 warn{};
        ImVec4 info{};
        ImVec4 debug{};
        ImVec4 trace{};
        ImVec4 fallback{};
    };

    struct ThemePalette
    {
        LogColorPalette log{};
    };

    inline void ApplyTheme(ThemeKind kind)
    {
        switch (kind)
        {
        case ThemeKind::Darkness:
            embraceTheDarknessTheme();
            break;
        case ThemeKind::Lightness:
            embraceTheLightnessTheme();
            break;
        }
    }

    inline ThemePalette BuildThemePalette(ThemeKind kind)
    {
        ThemePalette palette{};
        const ImVec4 base = ImGui::GetStyle().Colors[ImGuiCol_Text];

        palette.log.info = base;
        palette.log.fallback = base;

        if (kind == ThemeKind::Darkness)
        {
            palette.log.critical = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            palette.log.error = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            palette.log.warn = ImVec4(1.0f, 0.75f, 0.0f, 1.0f);
            palette.log.debug = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            palette.log.trace = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
        }
        else
        {
            palette.log.critical = ImVec4(0.75f, 0.1f, 0.1f, 1.0f);
            palette.log.error = ImVec4(0.65f, 0.0f, 0.0f, 1.0f);
            palette.log.warn = ImVec4(0.70f, 0.45f, 0.0f, 1.0f);
            palette.log.debug = ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
            palette.log.trace = ImVec4(0.20f, 0.45f, 0.75f, 1.0f);
        }

        return palette;
    }

} // namespace hybrid::ui
