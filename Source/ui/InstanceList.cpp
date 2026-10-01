#include "InstanceList.h"
#include "Theme.h"

namespace fabcutie::ui
{
    class InstanceList::RowComponent final : public juce::Component
    {
    public:
        explicit RowComponent (InstanceList& o) : owner (o)
        {
            name.setEditable (false, true, false);
            name.setTooltip ("Double-click to rename");
            name.setFont (juce::FontOptions (13.0f));
            name.setColour (juce::Label::textColourId, colours::text);
            name.onTextChange = [this]
            {
                if (owner.onRename)
                    owner.onRename (row.number, name.getText());
            };
            addAndMakeVisible (name);

            show.setClickingTogglesState (true);
            show.setTooltip ("Overlay this instance's curve on the graph");
            show.onClick = [this]
            {
                if (owner.onShowChanged)
                    owner.onShowChanged (row.number, show.getToggleState());
            };
            addAndMakeVisible (show);

            edit.setTooltip ("Edit this instance in this window");
            edit.onClick = [this]
            {
                if (owner.onEdit)
                    owner.onEdit (row.number);
            };
            addAndMakeVisible (edit);
        }

        void setRow (const Row& r)
        {
            row = r;

            if (! name.isBeingEdited())
                name.setText (row.name, juce::dontSendNotification);

            show.setToggleState (row.shown, juce::dontSendNotification);
            show.setEnabled (! row.isEditing);
            edit.setEnabled (! row.isEditing);
            edit.setButtonText (row.isEditing ? "Editing" : "Edit");
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto colour = instanceColour (row.number);

            if (row.isEditing)
            {
                g.setColour (colour.withAlpha (0.12f));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f, 1.0f), 4.0f);
            }

            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ 14.0f, (float) getHeight() * 0.5f }));

            if (row.isOwn)
            {
                g.setColour (colours::textDim);
                g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
                g.drawText ("THIS", tagArea, juce::Justification::centredRight);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (4, 3);
            area.removeFromLeft (22);
            edit.setBounds (area.removeFromRight (64));
            area.removeFromRight (6);
            show.setBounds (area.removeFromRight (52));
            area.removeFromRight (4);
            tagArea = area.removeFromRight (34);
            name.setBounds (area);
        }

    private:
        InstanceList& owner;
        Row row;
        juce::Label name;
        juce::TextButton show { "Show" }, edit { "Edit" };
        juce::Rectangle<int> tagArea;
    };

    InstanceList::InstanceList()
    {
        closeButton.onClick = [this] { if (onClose) onClose(); };
        addAndMakeVisible (closeButton);
    }

    InstanceList::~InstanceList() = default;

    void InstanceList::setRows (const std::vector<Row>& newRows)
    {
        if (newRows == rows)
            return;

        rows = newRows;

        while (rowComponents.size() > rows.size())
            rowComponents.pop_back();

        while (rowComponents.size() < rows.size())
        {
            rowComponents.push_back (std::make_unique<RowComponent> (*this));
            addAndMakeVisible (*rowComponents.back());
        }

        for (size_t i = 0; i < rows.size(); ++i)
            rowComponents[i]->setRow (rows[i]);

        if (getHeight() != getPreferredHeight())
            setSize (getWidth(), getPreferredHeight());
        else
            resized();

        repaint();
    }

    int InstanceList::getPreferredHeight() const noexcept
    {
        return titleHeight + rowHeight * juce::jmax (2, (int) rows.size()) + 10; // room for the hint when alone
    }

    void InstanceList::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bounds, 6.0f);
        g.setColour (colours::panelOutline);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("INSTANCES", getLocalBounds().removeFromTop (titleHeight).reduced (12, 0), juce::Justification::centredLeft);

        if (rows.size() <= 1)
        {
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Add FabCutie to other tracks to see them here.",
                        getLocalBounds().withTrimmedTop (titleHeight + rowHeight).withHeight (rowHeight).reduced (12, 0),
                        juce::Justification::centredLeft);
        }
    }

    void InstanceList::resized()
    {
        auto area = getLocalBounds().reduced (4, 0);
        auto title = area.removeFromTop (titleHeight);
        closeButton.setBounds (title.removeFromRight (60).reduced (2, 5));

        for (auto& row : rowComponents)
            row->setBounds (area.removeFromTop (rowHeight));
    }
}
