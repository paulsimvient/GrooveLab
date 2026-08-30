#include "EvolutionLab.h"

EvolutionLab::EvolutionLab(groove::GrooveEngine& e)
    : engine(e)
{
    setOpaque(true);
}

void EvolutionLab::drawPanel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
{
    g.setColour(juce::Colour(0xff0a151e));
    g.fillRoundedRectangle(r.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff1c3443));
    g.drawRoundedRectangle(r.toFloat(), 5.0f, 1.0f);

    auto header = r.removeFromTop(34);
    g.setColour(juce::Colour(0xff0d1c27));
    g.fillRoundedRectangle(header.toFloat(), 5.0f);
    g.setColour(juce::Colour(0xff2c98e8));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText(title, header.reduced(12, 0), juce::Justification::centredLeft);
}

void EvolutionLab::drawAncestry(juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& nodes = engine.ancestry().nodes();
    const int current = engine.ancestry().currentNodeId();
    r.removeFromTop(42);
    r = r.reduced(14);
    if (nodes.empty())
    {
        g.setColour(juce::Colour(0xff698494));
        g.drawText("Capture or evolve to create lineage.", r, juce::Justification::centred);
        return;
    }
    const int n = juce::jmin(7, (int) nodes.size());
    const int cx = r.getCentreX();
    const int top = r.getY() + 15;
    for (int i = 0; i < n; ++i)
    {
        const auto& node = nodes[nodes.size() - 1 - (size_t) i];
        const int y = top + i * 34;
        const int x = cx + (i % 2 == 0 ? -28 : 28);
        if (i > 0)
        {
            g.setColour(juce::Colour(0xff35566a));
            g.drawLine((float) cx, (float) (y - 24), (float) x, (float) y, 1);
        }
        auto box = juce::Rectangle<int>(x - 20, y - 13, 40, 26);
        g.setColour(node.id == current ? juce::Colour(0xff194c6b) : juce::Colour(0xff111f29));
        g.fillRoundedRectangle(box.toFloat(), 10);
        g.setColour(node.id == current ? juce::Colour(0xff7ac8ff) : juce::Colour(0xff587486));
        g.drawRoundedRectangle(box.toFloat(), 10, 1);
        g.setColour(juce::Colour(0xffd8e8f1));
        g.drawText(juce::String(node.id), box, juce::Justification::centred);
    }
}

void EvolutionLab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff050c12));
    drawPanel(g, evolutionPanel, "EVOLUTION");
    drawPanel(g, rulesPanel, "RULES");
    drawPanel(g, ancestryPanel, "ANCESTRY");
    drawAncestry(g, ancestryPanel);

    g.setColour(juce::Colour(0xff829bab));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    auto ep = evolutionPanel.reduced(14);
    ep.removeFromTop(46);
    g.drawText("SIMILARITY", ep.getX(), ep.getY(), 88, 31, juce::Justification::centredLeft);
    ep.removeFromTop(31);
    g.drawText("SURPRISE", ep.getX(), ep.getY(), 88, 35, juce::Justification::centredLeft);
    ep.removeFromTop(35);
    g.drawText("LOCK", ep.getX(), ep.getY(), 88, 35, juce::Justification::centredLeft);
    ep.removeFromTop(35);
    g.drawText("AMOUNT", ep.getX(), ep.getY(), 88, 35, juce::Justification::centredLeft);
}

void EvolutionLab::resized()
{
    auto r = getLocalBounds().reduced(12);
    const int gap = 10;
    evolutionPanel = r.removeFromLeft(juce::jmax(280, (int) (r.getWidth() * 0.42f)));
    r.removeFromLeft(gap);
    rulesPanel = r.removeFromLeft(juce::jmax(200, (int) (r.getWidth() * 0.38f)));
    r.removeFromLeft(gap);
    ancestryPanel = r;
    if (onLayout)
        onLayout();
}

EvolutionWindow::EvolutionWindow(groove::GrooveEngine& engine, juce::LookAndFeel& look)
    : DocumentWindow("Evolution",
                     juce::Colour(0xff050c12),
                     DocumentWindow::closeButton | DocumentWindow::minimiseButton),
      lab(engine)
{
    setUsingNativeTitleBar(true);
    setLookAndFeel(&look);
    lab.setLookAndFeel(&look);
    setContentNonOwned(&lab, false);
    setResizable(true, false);
    setResizeLimits(780, 320, 1600, 640);
    centreWithSize(1100, 380);
}

EvolutionWindow::~EvolutionWindow()
{
    clearContentComponent();
    setLookAndFeel(nullptr);
    lab.setLookAndFeel(nullptr);
}
