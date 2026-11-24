// The standalone app. It is JUCE's standard standalone app, except that the Options menu saves
// and opens songs as .ubeat files.

#include <juce_audio_utils/juce_audio_utils.h>

#if JucePlugin_Build_Standalone && JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{
constexpr auto songExtension = ".ubeat";
constexpr auto lastSongKey = "lastSongFile";

class UBeatWindow final : public juce::StandaloneFilterWindow
{
public:
    UBeatWindow (const juce::String& title, juce::Colour background, std::unique_ptr<juce::StandalonePluginHolder> holder,
                 juce::PropertiesFile& settingsIn)
        : StandaloneFilterWindow (title, background, std::move (holder)), settings (settingsIn)
    {
    }

private:
    enum MenuItem
    {
        audioSettings = 1,
        saveSong,
        openSong,
        reset
    };

    void buttonClicked (juce::Button* button) override
    {
        juce::PopupMenu menu;
        menu.addItem (audioSettings, "Audio/MIDI Settings...");
        menu.addSeparator();
        menu.addItem (saveSong, "Save song...");
        menu.addItem (openSong, "Open song...");
        menu.addSeparator();
        menu.addItem (reset, "Reset to default state");

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (button),
                            [safeThis = juce::Component::SafePointer (this)] (int result)
                            {
                                if (safeThis != nullptr)
                                    safeThis->handleItem (result);
                            });
    }

    void handleItem (int item)
    {
        switch (item)
        {
            case audioSettings: pluginHolder->showAudioSettingsDialog(); break;
            case saveSong:      askToSave(); break;
            case openSong:      askToOpen(); break;
            case reset:         resetToDefaultState(); break;
            default:            break;
        }
    }

    juce::File lastSong() const
    {
        const juce::File file (settings.getValue (lastSongKey));
        return file != juce::File() ? file : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    }

    void askToSave()
    {
        chooser = std::make_unique<juce::FileChooser> ("Save song", lastSong(), juce::String ("*") + songExtension);
        const auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting;

        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto chosen = fc.getResult();
            if (chosen == juce::File())
                return;

            const auto file = chosen.withFileExtension (songExtension);
            juce::MemoryBlock data;
            pluginHolder->processor->getStateInformation (data);

            if (file.replaceWithData (data.getData(), data.getSize()))
                settings.setValue (lastSongKey, file.getFullPathName());
            else
                showError ("Couldn't save the song", "μBeat couldn't write to " + file.getFullPathName());
        });
    }

    void askToOpen()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open song", lastSong(), juce::String ("*") + songExtension);
        const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File())
                return;

            juce::MemoryBlock data;
            if (! file.loadFileAsData (data) || data.isEmpty())
            {
                showError ("Couldn't open the song", "μBeat couldn't read " + file.getFullPathName());
                return;
            }

            pluginHolder->processor->setStateInformation (data.getData(), static_cast<int> (data.getSize()));
            settings.setValue (lastSongKey, file.getFullPathName());
        });
    }

    void showError (const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle (title)
                                          .withMessage (message)
                                          .withButton ("OK", 0)
                                          .withAssociatedComponent (this),
                                      nullptr);
    }

    juce::PropertiesFile& settings;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Follows JUCE's StandaloneFilterApp (juce_audio_plugin_client_Standalone.cpp) for macOS.
class UBeatApp final : public juce::JUCEApplication
{
public:
    UBeatApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "";
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto holder = std::make_unique<juce::StandalonePluginHolder> (appProperties.getUserSettings(), false);
        const auto background = juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId);
        mainWindow = std::make_unique<UBeatWindow> (getApplicationName(), background, std::move (holder),
                                                    *appProperties.getUserSettings());
        mainWindow->setVisible (true);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<UBeatWindow> mainWindow;
};
} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new UBeatApp();
}

#endif
