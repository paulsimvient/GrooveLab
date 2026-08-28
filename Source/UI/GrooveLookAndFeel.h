#pragma once
#include <JuceHeader.h>

class GrooveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GrooveLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff071018));
        setColour(juce::Label::textColourId, juce::Colour(0xffb7cddd));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff10202c));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff0f80d8));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd5ebf7));
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0c1822));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff213848));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffc5d8e4));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff208ee0));
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff243847));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff7ac8ff));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff208ee0));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2a36));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffbfd7e5));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff071018));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0c1822));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xffd8eaf4));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff213848));
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xff2c98e8));
        setColour(juce::TextEditor::highlightColourId, juce::Colour(0xff0f80d8));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0c1822));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffd5ebf7));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff0f80d8));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override
    {
        auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
        auto c = backgroundColour;

        if (down) c = c.brighter(0.18f);
        else if (highlighted) c = c.brighter(0.08f);

        g.setColour(c);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xff284353));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        auto size = (float) juce::jmin(width, height) - 10.0f;
        auto r = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height)
                     .withSizeKeepingCentre(size, size);

        auto angle = startAngle + sliderPos * (endAngle - startAngle);

        g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
        g.drawEllipse(r, 5.0f);

        juce::Path arc;
        arc.addCentredArc(r.getCentreX(), r.getCentreY(),
                         r.getWidth() * 0.5f, r.getHeight() * 0.5f,
                         0.0f, startAngle, angle, true);

        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(arc, juce::PathStrokeType(5.0f,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));

        juce::Path pointer;
        auto radius = r.getWidth() * 0.34f;
        auto centre = r.getCentre();
        auto pt = centre + juce::Point<float>(std::sin(angle) * radius,
                                              -std::cos(angle) * radius);
        pointer.startNewSubPath(centre);
        pointer.lineTo(pt);

        g.setColour(slider.findColour(juce::Slider::thumbColourId));
        g.strokePath(pointer, juce::PathStrokeType(2.0f));
    }
};
