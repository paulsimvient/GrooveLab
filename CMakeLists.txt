cmake_minimum_required(VERSION 3.22)
project(GrooveLab VERSION 0.7.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
)
FetchContent_MakeAvailable(JUCE)

set(GROOVELAB_CORE
    Source/Core/GrooveTypes.h
    Source/Core/GrooveState.h
    Source/Core/GrooveState.cpp
    Source/Core/EventJournal.h
    Source/Core/EventJournal.cpp
    Source/Core/EvolutionEngine.h
    Source/Core/EvolutionEngine.cpp
    Source/Core/Ancestry.h
    Source/Core/Ancestry.cpp
    Source/Sequencer/Sequencer.h
    Source/Sequencer/Sequencer.cpp
    Source/Audio/DrumSynth.h
    Source/Audio/DrumSynth.cpp
    Source/Audio/DrumMidi.h
    Source/Audio/GrooveEngine.h
    Source/Audio/GrooveEngine.cpp
)

juce_add_gui_app(GrooveLab
    PRODUCT_NAME "Lil God Projector"
    COMPANY_NAME "Lil God Projector"
    BUNDLE_ID "com.groovelab.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/GrooveLabIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/GrooveLabIcon.png"
)

target_sources(GrooveLab PRIVATE
    ${GROOVELAB_CORE}
    Source/Audio/ExternalPluginHost.h
    Source/Audio/ExternalPluginHost.cpp
    Source/Audio/MixBus.h
    Source/Audio/MixBus.cpp
    Source/UI/GrooveLookAndFeel.h
    Source/UI/MixStrip.h
    Source/UI/MixStrip.cpp
    Source/UI/TorsoPage.h
    Source/UI/TorsoPage.cpp
    Source/UI/SongPage.h
    Source/UI/SongPage.cpp
    Source/UI/SynthKeyboard.h
    Source/UI/SynthKeyboard.cpp
    Source/UI/EvolutionLab.h
    Source/UI/EvolutionLab.cpp
    Source/UI/MainComponent.h
    Source/UI/MainComponent.cpp
    Source/Standalone/Main.cpp
)

target_compile_definitions(GrooveLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

juce_add_binary_data(GrooveLabAssets SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/GrooveLabIcon.png"
)

target_link_libraries(GrooveLab PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    GrooveLabAssets
)

juce_add_plugin(GrooveLabPlugin
    COMPANY_NAME "Lil God Projector"
    BUNDLE_ID "com.groovelab.plugin"
    PLUGIN_MANUFACTURER_CODE GrLb
    PLUGIN_CODE Gr01
    FORMATS AU VST3
    PRODUCT_NAME "Lil God Projector"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/GrooveLabIcon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/GrooveLabIcon.png"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(GrooveLabPlugin PRIVATE
    ${GROOVELAB_CORE}
    Source/Plugin/PluginProcessor.h
    Source/Plugin/PluginProcessor.cpp
    Source/Plugin/PluginEditor.h
    Source/Plugin/PluginEditor.cpp
)

target_compile_definitions(GrooveLabPlugin PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(GrooveLabPlugin PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
)

juce_generate_juce_header(GrooveLab)
juce_generate_juce_header(GrooveLabPlugin)
