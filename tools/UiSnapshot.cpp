// Renders each tab of the editor to a PNG, for checking the UI without opening a window.
// Usage: UiSnapshot <output-directory>

#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace
{
juce::TextButton* findButton (juce::Component& root, const juce::String& text)
{
    for (auto* child : root.getChildren())
    {
        if (auto* button = dynamic_cast<juce::TextButton*> (child); button != nullptr && button->getButtonText() == text)
            return button;
        if (auto* found = findButton (*child, text))
            return found;
    }
    return nullptr;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outputDirectory (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outputDirectory.createDirectory();

    SynthAudioProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // A decaying chirp in two slots so the samples page has waveforms to draw.
    for (int slot = 0; slot < 2; ++slot)
    {
        juce::AudioBuffer<float> chirp (1, 30000);
        for (int i = 0; i < chirp.getNumSamples(); ++i)
        {
            const auto t = static_cast<double> (i);
            const auto value = 0.8 * std::exp (-t / (6000.0 + 6000.0 * slot)) * std::sin (0.002 * t * (1.0 + t / 20000.0))
                               * (1.0 + 0.3 * std::sin (t / 700.0));
            chirp.setSample (0, i, static_cast<float> (value));
        }
        processor.groovebox.getSampler().setSample (slot, chirp, 48000.0);
        for (int step = slot * 2; step < 32; step += 8)
            processor.bank.setSampleStep (0, slot, step, true);
    }

    // Play for a moment so the playhead shows.
    processor.groovebox.setPlaying (true);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 60; ++i)
        processor.processBlock (buffer, midi);

    processor.groovebox.setLiveTrack (1); // Strings: shows held notes on the melody page
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1240, 800);

    for (auto* tab : { "DRUMS", "SAMPLES", "MELODY", "SOUND", "MIX" })
    {
        if (auto* button = findButton (*editor, tab))
        {
            button->setToggleState (true, juce::dontSendNotification);
            button->onClick();
        }

        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outputDirectory.getChildFile (juce::String (tab).toLowerCase() + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << file.getFullPathName() << std::endl;
    }

    editor.reset();
    return 0;
}
