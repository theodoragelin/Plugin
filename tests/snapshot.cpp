// Renders the plugin editor to PNG files (one per preset style) for design review.
// Run under a display (e.g. xvfb-run) on Linux.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    SeleneProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    for (int preset : { 0, 3, 4, 6 })
    {
        proc.setCurrentProgram (preset);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        // Let the animation run for a moment so smoothing/glow settle.
        for (int i = 0; i < 20; ++i)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto file = outDir.getChildFile ("ui_preset_" + juce::String (preset) + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    }
    return 0;
}
