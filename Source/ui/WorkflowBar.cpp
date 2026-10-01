#include "WorkflowBar.h"
#include "Theme.h"

namespace fabcutie::ui
{
    // A small square button drawn with a vector icon.
    class WorkflowBar::IconButton final : public juce::Button
    {
    public:
        enum class Icon { undo, redo, previous, next, more };

        IconButton (Icon i, const juce::String& tooltip) : juce::Button (tooltip), icon (i)
        {
            setTooltip (tooltip);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

            if (over || down)
            {
                g.setColour (colours::control.brighter (down ? 0.2f : 0.08f));
                g.fillRoundedRectangle (bounds, 4.0f);
            }

            g.setColour (isEnabled() ? colours::text.withAlpha (over ? 1.0f : 0.8f) : colours::textDim.withAlpha (0.3f));

            const auto c = bounds.getCentre();
            const auto r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.26f;
            const juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            juce::Path p;

            switch (icon)
            {
                case Icon::undo:
                case Icon::redo:
                {
                    // A three-quarter circle with an arrowhead on its open end.
                    const auto flip = icon == Icon::redo ? -1.0f : 1.0f;
                    p.addCentredArc (c.x, c.y + 1.0f, r, r, 0.0f, flip * -2.2f, flip * 1.6f, true);
                    g.strokePath (p, stroke);

                    const auto start = p.getPointAlongPath (0.0f);
                    juce::Path head;
                    head.addTriangle (start.x - flip * 3.5f, start.y - 1.0f, start.x + flip * 2.5f, start.y - 3.5f,
                                      start.x + flip * 1.5f, start.y + 3.0f);
                    g.fillPath (head);
                    break;
                }

                case Icon::previous:
                case Icon::next:
                {
                    const auto dir = icon == Icon::next ? 1.0f : -1.0f;
                    p.startNewSubPath (c.x - dir * r * 0.5f, c.y - r);
                    p.lineTo (c.x + dir * r * 0.5f, c.y);
                    p.lineTo (c.x - dir * r * 0.5f, c.y + r);
                    g.strokePath (p, stroke);
                    break;
                }

                case Icon::more:
                    for (int i = -1; i <= 1; ++i)
                        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ c.x + (float) i * 5.0f, c.y }));
                    break;
            }
        }

    private:
        Icon icon;
    };

    WorkflowBar::WorkflowBar (workflow::History& h, workflow::ABCompare& a, workflow::Presets& p, workflow::MidiLearn& m)
        : history (h), ab (a), presets (p), midi (m)
    {
        using Icon = IconButton::Icon;

        undoButton     = std::make_unique<IconButton> (Icon::undo, "Undo");
        redoButton     = std::make_unique<IconButton> (Icon::redo, "Redo");
        previousButton = std::make_unique<IconButton> (Icon::previous, "Previous preset");
        nextButton     = std::make_unique<IconButton> (Icon::next, "Next preset");
        settingsButton = std::make_unique<IconButton> (Icon::more, "Window size and MIDI");

        undoButton->onClick     = [this] { history.undo(); update(); };
        redoButton->onClick     = [this] { history.redo(); update(); };
        previousButton->onClick = [this] { presets.loadNext (-1); update(); };
        nextButton->onClick     = [this] { presets.loadNext (+1); update(); };
        settingsButton->onClick = [this] { showSettingsMenu(); };

        for (auto* b : { undoButton.get(), redoButton.get(), previousButton.get(), nextButton.get(), settingsButton.get() })
            addAndMakeVisible (*b);

        presetButton.setTooltip ("Presets");
        presetButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        presetButton.onClick = [this] { showPresetMenu(); };
        addAndMakeVisible (presetButton);

        aButton.setTooltip ("Settings A");
        bButton.setTooltip ("Settings B");
        copyButton.setTooltip ("Copy the current settings to the other slot");

        for (auto* b : { &aButton, &bButton })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, colours::accent);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            addAndMakeVisible (*b);
        }

        aButton.onClick = [this] { ab.select (0); update(); };
        bButton.onClick = [this] { ab.select (1); update(); };
        copyButton.onClick = [this] { ab.copyToOther(); };
        addAndMakeVisible (copyButton);

        update();
        startTimerHz (10);
    }

    WorkflowBar::~WorkflowBar() = default;

    void WorkflowBar::timerCallback()
    {
        update();
    }

    void WorkflowBar::update()
    {
        undoButton->setEnabled (history.canUndo());
        redoButton->setEnabled (history.canRedo());

        const auto name = presets.getCurrentName();
        presetButton.setButtonText (name.isEmpty() ? "No preset" : name);

        aButton.setToggleState (ab.getActive() == 0, juce::dontSendNotification);
        bButton.setToggleState (ab.getActive() == 1, juce::dontSendNotification);
        copyButton.setButtonText (ab.getActive() == 0 ? "A>B" : "B>A");
    }

    void WorkflowBar::showPresetMenu()
    {
        presets.rescan();
        const auto& entries = presets.getEntries();

        juce::PopupMenu menu;
        menu.addSectionHeader ("Factory");

        for (int i = 0; i < (int) entries.size(); ++i)
        {
            if (i == presets.getNumFactory())
                menu.addSectionHeader ("User");

            menu.addItem (100 + i, entries[(size_t) i].name, true, i == presets.getCurrent());
        }

        if ((int) entries.size() == presets.getNumFactory())
        {
            menu.addSectionHeader ("User");
            menu.addItem (-1, "No user presets yet", false);
        }

        const auto current = presets.getCurrent();
        const auto canDelete = current >= 0 && ! entries[(size_t) current].factory;

        menu.addSeparator();
        menu.addItem (1, "Save preset...");
        menu.addItem (2, canDelete ? "Delete \"" + entries[(size_t) current].name + "\"" : "Delete preset", canDelete);
        menu.addItem (3, "Show presets folder");

        juce::Component::SafePointer<WorkflowBar> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton),
                            [safeThis, current] (int result)
                            {
                                if (safeThis == nullptr || result == 0)
                                    return;

                                auto& p = safeThis->presets;

                                if (result >= 100)
                                {
                                    p.load (result - 100);
                                }
                                else if (result == 1)
                                {
                                    safeThis->askPresetName();
                                }
                                else if (result == 2)
                                {
                                    p.remove (current);
                                }
                                else if (result == 3)
                                {
                                    const auto folder = p.getUserFolder();
                                    folder.createDirectory();
                                    folder.startAsProcess();
                                }

                                safeThis->update();
                            });
    }

    void WorkflowBar::askPresetName()
    {
        auto* parent = getParentComponent();

        if (parent == nullptr)
            return;

        auto* window = new juce::AlertWindow ("Save preset", "Name:", juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", presets.getCurrentName());
        window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        parent->addAndMakeVisible (window);
        window->setCentrePosition (parent->getLocalBounds().getCentre());

        if (auto* editor = window->getTextEditor ("name"))
        {
            editor->setInputRestrictions (64, {});
            editor->selectAll();
            editor->grabKeyboardFocus();
        }

        juce::Component::SafePointer<WorkflowBar> safeThis (this);
        juce::Component::SafePointer<juce::AlertWindow> safeWindow (window);

        window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, safeWindow] (int result)
        {
            if (result != 1 || safeThis == nullptr || safeWindow == nullptr)
                return;

            const auto name = safeWindow->getTextEditorContents ("name");

            if (! safeThis->presets.save (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                        "Could not save \"" + name + "\". Names cannot be empty or contain / \\ : * ? \" < > |");

            safeThis->update();
        }), true);
    }

    void WorkflowBar::showSettingsMenu()
    {
        juce::PopupMenu sizeMenu;
        const auto* editor = getParentComponent();
        const char* names[] { "Small", "Medium", "Large", "Extra large" };

        for (size_t i = 0; i < sizes.size(); ++i)
        {
            const auto [w, h] = sizes[i];
            const auto current = editor != nullptr && editor->getWidth() == w && editor->getHeight() == h;
            sizeMenu.addItem (10 + (int) i, juce::String (names[i]) + " (" + juce::String (w) + " x " + juce::String (h) + ")",
                              true, current);
        }

        juce::PopupMenu menu;
        menu.addSubMenu ("Window size", sizeMenu);
        menu.addSeparator();
        menu.addSectionHeader ("MIDI");
        menu.addItem (2, "Cancel MIDI learn", midi.getLearning() >= 0);
        menu.addItem (1, "Forget all MIDI controllers", midi.hasAssignments());
        menu.addItem (-1, "Right-click a knob to MIDI learn it", false);

        juce::Component::SafePointer<WorkflowBar> safeThis (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton.get()),
                            [safeThis] (int result)
                            {
                                if (safeThis == nullptr)
                                    return;

                                if (result == 1)
                                    safeThis->midi.clearAll();
                                else if (result == 2)
                                    safeThis->midi.cancelLearn();
                                else if (result >= 10 && result < 10 + (int) sizes.size() && safeThis->onSizeChosen)
                                    safeThis->onSizeChosen (sizes[(size_t) (result - 10)].first, sizes[(size_t) (result - 10)].second);
                            });
    }

    void WorkflowBar::paint (juce::Graphics& g)
    {
        // A faint well behind the preset name.
        if (! presetButton.isVisible())
            return;

        g.setColour (colours::control.withAlpha (0.5f));
        g.fillRoundedRectangle (presetButton.getBounds().toFloat().expanded (24.0f, 0.0f).withX ((float) previousButton->getX())
                                    .withRight ((float) nextButton->getRight()), 4.0f);
    }

    void WorkflowBar::resized()
    {
        auto area = getLocalBounds();
        const auto square = juce::jmin (26, area.getHeight());

        auto place = [&] (juce::Component& c, int width, int height)
        {
            c.setBounds (area.removeFromLeft (width).withSizeKeepingCentre (width, height));
        };

        place (*undoButton, square, square);
        area.removeFromLeft (2);
        place (*redoButton, square, square);
        area.removeFromLeft (12);

        // The preset browser takes what the A/B and settings buttons leave,
        // up to a comfortable width, and hides when there is too little.
        constexpr int abWidth = 12 + 24 + 2 + 24 + 4 + 40 + 10;
        const auto presetWidth = juce::jlimit (0, 240, area.getWidth() - abWidth - square);
        const auto showPresets = presetWidth >= 4 * square;

        for (auto* c : { (juce::Component*) previousButton.get(), (juce::Component*) nextButton.get(), (juce::Component*) &presetButton })
            c->setVisible (showPresets);

        if (showPresets)
        {
            auto presetArea = area.removeFromLeft (presetWidth);
            previousButton->setBounds (presetArea.removeFromLeft (square).withSizeKeepingCentre (square, square));
            nextButton->setBounds (presetArea.removeFromRight (square).withSizeKeepingCentre (square, square));
            presetButton.setBounds (presetArea.withSizeKeepingCentre (presetArea.getWidth(), square));
            area.removeFromLeft (12);
        }

        place (aButton, 24, 22);
        area.removeFromLeft (2);
        place (bButton, 24, 22);
        area.removeFromLeft (4);
        place (copyButton, 40, 22);
        area.removeFromLeft (10);
        place (*settingsButton, square, square);
    }
}
