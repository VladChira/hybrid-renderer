#pragma once

#include <string>
#include <vector>

namespace hybrid::ui
{

    enum class DockTarget
    {
        Main,

        LeftTop,
        LeftBottom,

        RightTop,
        RightBottom,

        BottomLeft,
        BottomRight
    };

    struct DockAssignment
    {
        std::string panel_title;
        DockTarget target = DockTarget::Main;
    };

    struct DockspaceLayout
    {
        float left_ratio = 0.15f;
        float left_split_ratio = 0.5f;

        float right_ratio = 0.3f;
        float right_split_ratio = 0.5f;

        float bottom_ratio = 0.3f;
        float bottom_split_ratio = 0.5f;
        std::vector<DockAssignment> assignments;

        static DockspaceLayout Default();
    };

    class Dockspace
    {
    public:
        void BeginFrame() const;
        void BuildLayout(const DockspaceLayout &layout);
        void ResetLayout() noexcept;

    private:
        bool m_layout_built = false;
    };

} // namespace hybrid::ui
