#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fabcutie::ui
{
    // A colour per instance number, stepped round the wheel so neighbouring
    // instances look different (offset from the band colours).
    inline juce::Colour instanceColour (int number)
    {
        const auto hue = std::fmod (0.55f + 0.381966f * (float) number, 1.0f);
        return juce::Colour::fromHSV (hue, 0.45f, 0.95f, 1.0f);
    }

    // The instance list: every FabCutie in the session, one row each. Show
    // overlays an instance's curve on this window's graph; Edit switches this
    // window to that instance. Double-click a name to rename it.
    class InstanceList final : public juce::Component
    {
    public:
        struct Row
        {
            int number = 0;
            juce::String name;
            bool isOwn = false;     // the instance this window belongs to
            bool isEditing = false; // the instance this window is showing
            bool shown = false;     // its curve is overlaid on the graph

            bool operator== (const Row& o) const
            {
                return number == o.number && name == o.name && isOwn == o.isOwn
                    && isEditing == o.isEditing && shown == o.shown;
            }
        };

        InstanceList();
        ~InstanceList() override;

        void setRows (const std::vector<Row>&);
        int getPreferredHeight() const noexcept;

        static constexpr int preferredWidth = 380;

        std::function<void (int number)> onEdit;
        std::function<void (int number, bool shown)> onShowChanged;
        std::function<void (int number, const juce::String& name)> onRename;
        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        static constexpr int titleHeight = 30;
        static constexpr int rowHeight = 30;

        class RowComponent;

        std::vector<Row> rows;
        std::vector<std::unique_ptr<RowComponent>> rowComponents;
        juce::TextButton closeButton { "Close" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstanceList)
    };
}
