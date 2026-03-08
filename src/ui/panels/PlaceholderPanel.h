#pragma once

#include "Panel.h"

#include <string>

namespace hybrid::ui
{

    class PlaceholderPanel final : public Panel
        {
        public:
            explicit PlaceholderPanel(std::string title)
                : Panel(std::move(title))
            {
            }

        private:
            void DrawContents(PanelContext &context) override
            {
                (void)context;
            }
        };

} // namespace hybrid::ui