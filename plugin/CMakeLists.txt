# The Capicola plugin: VST3, AU (macOS) and Standalone.

set(GRANULAR_PLUGIN_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GRANULAR_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(Capicola
    PRODUCT_NAME                "Capicola"
    COMPANY_NAME                "browncreativenz"
    BUNDLE_ID                   "nz.browncreative.capicola"
    PLUGIN_MANUFACTURER_CODE    Brcn
    PLUGIN_CODE                 Cpca
    FORMATS                     ${GRANULAR_PLUGIN_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE     # note-ons slice; makes the AU a music effect (aumf)
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Pitch\ Shift
    AU_MAIN_TYPE                kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD     ${GRANULAR_COPY_PLUGIN})

target_sources(Capicola PRIVATE PluginProcessor.cpp PluginEditor.cpp FollowerMidiOut.cpp)

# Interface fonts (SIL Open Font License; see resources/fonts).
juce_add_binary_data(CapicolaFonts SOURCES
    resources/fonts/IBMPlexSans-Regular.ttf
    resources/fonts/IBMPlexSans-Medium.ttf
    resources/fonts/IBMPlexSans-SemiBold.ttf
    resources/fonts/IBMPlexMono-Regular.ttf
    resources/fonts/IBMPlexMono-Medium.ttf
    resources/fonts/SpaceGrotesk-Bold.ttf)

target_compile_definitions(Capicola PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Capicola
    PRIVATE
        granular_core
        CapicolaFonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
