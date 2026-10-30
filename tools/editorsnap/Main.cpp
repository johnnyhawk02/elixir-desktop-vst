// Renders a plugin's editor to a PNG without a host, so its layout can be checked
// from the command line. Built per plugin as <Target>_Snapshot (see cmake/JohnnyPlugin.cmake).
//
//   build/plugins/<Name>/<Name>_Snapshot_artefacts/Release/<Name>_Snapshot out.png [--click "FULL"] [--scale 2]
//
// --click presses the first button with that text (repeatable), e.g. to switch views.

#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{
    juce::Button* findButton (juce::Component& root, const juce::String& text)
    {
        for (auto* child : root.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (child))
                if (b->isVisible() && b->getButtonText().equalsIgnoreCase (text))
                    return b;
            if (auto* found = findButton (*child, text))
                return found;
        }
        return nullptr;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: <Target>_Snapshot out.png [--click TEXT]... [--scale 2]\n");
        return 1;
    }

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    float scale = 2.0f;
    juce::StringArray clicks;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const juce::String key (argv[i]);
        if (key == "--click") clicks.add (juce::String::fromUTF8 (argv[i + 1]));
        else if (key == "--scale") scale = juce::String (argv[i + 1]).getFloatValue();
    }

    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    processor->setPlayConfigDetails (0, 2, 48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorIfNeeded());
    if (editor == nullptr)
    {
        std::fprintf (stderr, "plugin has no editor\n");
        return 1;
    }
    editor->setVisible (true);

    for (auto& text : clicks)
    {
        if (auto* b = findButton (*editor, text))
        {
            if (b->onClick) b->onClick();
        }
        else
            std::fprintf (stderr, "no visible button \"%s\"\n", text.toRawUTF8());
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::fprintf (stderr, "could not write %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("%s  %dx%d\n", out.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());
    editor = nullptr;
    return 0;
}
