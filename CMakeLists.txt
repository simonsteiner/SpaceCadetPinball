cmake_minimum_required(VERSION 3.24)
project(SpaceCadetPinball)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

# Windows and macOS have no standard SDL packages, so build SDL from source there by default
if(WIN32 OR APPLE)
    set(SCP_VENDORED_SDL_DEFAULT ON)
else()
    set(SCP_VENDORED_SDL_DEFAULT OFF)
endif()
option(SCP_VENDORED_SDL "Build SDL3 and SDL3_mixer from pinned sources instead of using installed packages" ${SCP_VENDORED_SDL_DEFAULT})
option(SCP_WARNINGS "Compile the game with extra warnings (-Wall -Wextra, or /W4 with MSVC) and treat them as errors" OFF)
option(SCP_SANITIZERS "Build the game with AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang)" OFF)

# Link mingw libs static
if(MINGW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13")
endif()

if(SCP_VENDORED_SDL)
    include(FetchContent)

    # Static libraries, nothing installed, no tests or examples
    set(BUILD_SHARED_LIBS OFF)
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST_LIBRARY OFF)
    set(SDL_TESTS OFF)
    set(SDL_EXAMPLES OFF)
    set(SDL_INSTALL OFF)
    set(SDL_UNINSTALL OFF)

    # The game only plays WAV sounds and MIDI music. Timidity is SDL_mixer's only MIDI decoder
    # that doesn't need a system library.
    set(SDLMIXER_INSTALL OFF)
    set(SDLMIXER_TESTS OFF)
    set(SDLMIXER_EXAMPLES OFF)
    set(SDLMIXER_VENDORED OFF)
    set(SDLMIXER_DEPS_SHARED OFF)
    set(SDLMIXER_AIFF OFF)
    set(SDLMIXER_VOC OFF)
    set(SDLMIXER_AU OFF)
    set(SDLMIXER_FLAC OFF)
    set(SDLMIXER_GME OFF)
    set(SDLMIXER_MOD OFF)
    set(SDLMIXER_MP3 OFF)
    set(SDLMIXER_OPUS OFF)
    set(SDLMIXER_VORBIS_STB OFF)
    set(SDLMIXER_VORBIS_VORBISFILE OFF)
    set(SDLMIXER_VORBIS_TREMOR OFF)
    set(SDLMIXER_WAVPACK OFF)
    set(SDLMIXER_WAVE ON)
    set(SDLMIXER_MIDI ON)
    set(SDLMIXER_MIDI_FLUIDSYNTH OFF)
    set(SDLMIXER_MIDI_TIMIDITY ON)

    FetchContent_Declare(SDL3
            URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
            URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
            )
    FetchContent_Declare(SDL3_mixer
            URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-3.2.4.tar.gz
            URL_HASH SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901
            )
    FetchContent_MakeAvailable(SDL3 SDL3_mixer)

    set(SDL_LIBRARIES SDL3_mixer::SDL3_mixer-static SDL3::SDL3-static)
else()
    find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3)
    find_package(SDL3_mixer 3.2 REQUIRED CONFIG)

    set(SDL_LIBRARIES SDL3_mixer::SDL3_mixer SDL3::SDL3)
endif()



set(SOURCE_FILES
        SpaceCadetPinball/control.cpp
        SpaceCadetPinball/control.h
        SpaceCadetPinball/EmbeddedData.cpp
        SpaceCadetPinball/EmbeddedData.h
        SpaceCadetPinball/EmbeddedDataDecompress.cpp
        SpaceCadetPinball/font_selection.cpp
        SpaceCadetPinball/font_selection.h
        SpaceCadetPinball/fullscrn.cpp
        SpaceCadetPinball/fullscrn.h
        SpaceCadetPinball/gdrv.cpp
        SpaceCadetPinball/gdrv.h
        SpaceCadetPinball/GroupData.cpp
        SpaceCadetPinball/GroupData.h
        SpaceCadetPinball/high_score.cpp
        SpaceCadetPinball/high_score.h
        SpaceCadetPinball/loader.cpp
        SpaceCadetPinball/loader.h
        SpaceCadetPinball/maths.cpp
        SpaceCadetPinball/maths.h
        SpaceCadetPinball/midi.cpp
        SpaceCadetPinball/midi.h
        SpaceCadetPinball/nudge.cpp
        SpaceCadetPinball/nudge.h
        SpaceCadetPinball/options.cpp
        SpaceCadetPinball/options.h
        SpaceCadetPinball/partman.cpp
        SpaceCadetPinball/partman.h
        SpaceCadetPinball/pb.cpp
        SpaceCadetPinball/pb.h
        SpaceCadetPinball/pch.h
        SpaceCadetPinball/PostEffect.cpp
        SpaceCadetPinball/PostEffect.h
        SpaceCadetPinball/shaders/PostEffectShaders.h
        SpaceCadetPinball/proj.cpp
        SpaceCadetPinball/proj.h
        SpaceCadetPinball/render.cpp
        SpaceCadetPinball/render.h
        SpaceCadetPinball/replay.cpp
        SpaceCadetPinball/replay.h
        SpaceCadetPinball/Rumble.cpp
        SpaceCadetPinball/Rumble.h
        SpaceCadetPinball/score.cpp
        SpaceCadetPinball/score.h
        SpaceCadetPinball/Sound.cpp
        SpaceCadetPinball/Sound.h
        SpaceCadetPinball/SpaceCadetPinball.cpp
        SpaceCadetPinball/TBall.cpp
        SpaceCadetPinball/TBall.h
        SpaceCadetPinball/TBlocker.cpp
        SpaceCadetPinball/TBlocker.h
        SpaceCadetPinball/TBumper.cpp
        SpaceCadetPinball/TBumper.h
        SpaceCadetPinball/TCircle.cpp
        SpaceCadetPinball/TCircle.h
        SpaceCadetPinball/TCollisionComponent.cpp
        SpaceCadetPinball/TCollisionComponent.h
        SpaceCadetPinball/TComponentGroup.cpp
        SpaceCadetPinball/TComponentGroup.h
        SpaceCadetPinball/TDemo.cpp
        SpaceCadetPinball/TDemo.h
        SpaceCadetPinball/TDrain.cpp
        SpaceCadetPinball/TDrain.h
        SpaceCadetPinball/TEdgeBox.h
        SpaceCadetPinball/TEdgeManager.cpp
        SpaceCadetPinball/TEdgeManager.h
        SpaceCadetPinball/TEdgeSegment.cpp
        SpaceCadetPinball/TEdgeSegment.h
        SpaceCadetPinball/TFlagSpinner.cpp
        SpaceCadetPinball/TFlagSpinner.h
        SpaceCadetPinball/TFlipper.cpp
        SpaceCadetPinball/TFlipper.h
        SpaceCadetPinball/TFlipperEdge.cpp
        SpaceCadetPinball/TFlipperEdge.h
        SpaceCadetPinball/TGate.cpp
        SpaceCadetPinball/TGate.h
        SpaceCadetPinball/THole.cpp
        SpaceCadetPinball/THole.h
        SpaceCadetPinball/timer.cpp
        SpaceCadetPinball/timer.h
        SpaceCadetPinball/TKickback.cpp
        SpaceCadetPinball/TKickback.h
        SpaceCadetPinball/TKickout.cpp
        SpaceCadetPinball/TKickout.h
        SpaceCadetPinball/TLight.cpp
        SpaceCadetPinball/TLight.h
        SpaceCadetPinball/TLightBargraph.cpp
        SpaceCadetPinball/TLightBargraph.h
        SpaceCadetPinball/TLightGroup.cpp
        SpaceCadetPinball/TLightGroup.h
        SpaceCadetPinball/TLightRollover.cpp
        SpaceCadetPinball/TLightRollover.h
        SpaceCadetPinball/TLine.cpp
        SpaceCadetPinball/TLine.h
        SpaceCadetPinball/TOneway.cpp
        SpaceCadetPinball/TOneway.h
        SpaceCadetPinball/TPinballComponent.cpp
        SpaceCadetPinball/TPinballComponent.h
        SpaceCadetPinball/TPinballTable.cpp
        SpaceCadetPinball/TPinballTable.h
        SpaceCadetPinball/TPlunger.cpp
        SpaceCadetPinball/TPlunger.h
        SpaceCadetPinball/TPopupTarget.cpp
        SpaceCadetPinball/TPopupTarget.h
        SpaceCadetPinball/TRamp.cpp
        SpaceCadetPinball/TRamp.h
        SpaceCadetPinball/translations.cpp
        SpaceCadetPinball/translations.h
        SpaceCadetPinball/TRollover.cpp
        SpaceCadetPinball/TRollover.h
        SpaceCadetPinball/TSink.cpp
        SpaceCadetPinball/TSink.h
        SpaceCadetPinball/TSoloTarget.cpp
        SpaceCadetPinball/TSoloTarget.h
        SpaceCadetPinball/TSound.cpp
        SpaceCadetPinball/TSound.h
        SpaceCadetPinball/TTableLayer.cpp
        SpaceCadetPinball/TTableLayer.h
        SpaceCadetPinball/TTextBox.cpp
        SpaceCadetPinball/TTextBox.h
        SpaceCadetPinball/TTextBoxMessage.cpp
        SpaceCadetPinball/TTextBoxMessage.h
        SpaceCadetPinball/TTimer.cpp
        SpaceCadetPinball/TTimer.h
        SpaceCadetPinball/TTripwire.cpp
        SpaceCadetPinball/TTripwire.h
        SpaceCadetPinball/TWall.cpp
        SpaceCadetPinball/TWall.h
        SpaceCadetPinball/winmain.cpp
        SpaceCadetPinball/winmain.h
        SpaceCadetPinball/zdrv.cpp
        SpaceCadetPinball/zdrv.h
        SpaceCadetPinball/DebugOverlay.cpp
        SpaceCadetPinball/DebugOverlay.h
        )

# Vendored ImGui, a separate library so that it gets neither the game's precompiled header nor its warnings
set(IMGUI_SOURCE_FILES
        SpaceCadetPinball/imconfig.h
        SpaceCadetPinball/imgui_internal.h
        SpaceCadetPinball/imgui.cpp
        SpaceCadetPinball/imgui.h
        SpaceCadetPinball/imgui_draw.cpp
        SpaceCadetPinball/imgui_widgets.cpp
        SpaceCadetPinball/imgui_tables.cpp
        SpaceCadetPinball/imgui_demo.cpp
        SpaceCadetPinball/imgui_impl_sdl3.cpp
        SpaceCadetPinball/imgui_impl_sdl3.h
        SpaceCadetPinball/imgui_impl_sdlrenderer3.cpp
        SpaceCadetPinball/imgui_impl_sdlrenderer3.h
        SpaceCadetPinball/imstb_textedit.h
        SpaceCadetPinball/imstb_rectpack.h
        SpaceCadetPinball/imstb_truetype.h
        )

# On Windows, include resource file with the icon
if(WIN32)
    set_source_files_properties(SpaceCadetPinball/SpaceCadetPinball.rc LANGUAGE RC)
    list(APPEND SOURCE_FILES SpaceCadetPinball/SpaceCadetPinball.rc)
endif(WIN32)

add_library(imgui STATIC ${IMGUI_SOURCE_FILES})
target_link_libraries(imgui PUBLIC ${SDL_LIBRARIES})

# Vendored TinySoundFont plays MIDI music with a SoundFont where there is no native MIDI
add_library(tinysoundfont STATIC
        third_party/TinySoundFont/TinySoundFont.c
        third_party/TinySoundFont/tml.h
        third_party/TinySoundFont/tsf.h
        )
target_include_directories(tinysoundfont PUBLIC third_party/TinySoundFont)
if(UNIX)
    target_link_libraries(tinysoundfont PUBLIC m)
endif()

add_executable(SpaceCadetPinball ${SOURCE_FILES})
target_link_libraries(SpaceCadetPinball tinysoundfont)

# Vendored SDL_native_midi plays MIDI music through the system's synth on Windows and macOS
if(WIN32 OR APPLE)
    add_library(native_midi STATIC
            third_party/SDL_native_midi/include/SDL3_native_midi/SDL_native_midi.h
            third_party/SDL_native_midi/src/SDL_native_midi_common.c
            third_party/SDL_native_midi/src/SDL_native_midi_common.h
            third_party/SDL_native_midi/src/SDL_native_midi_macos.c
            third_party/SDL_native_midi/src/SDL_native_midi_win32.c
            )
    target_include_directories(native_midi PUBLIC third_party/SDL_native_midi/include)
    target_link_libraries(native_midi PUBLIC ${SDL_LIBRARIES})
    if(WIN32)
        target_link_libraries(native_midi PUBLIC winmm)
    else()
        target_link_libraries(native_midi PUBLIC "-framework AudioToolbox" "-framework AudioUnit" "-framework CoreServices")
    endif()
    target_link_libraries(SpaceCadetPinball native_midi)
    target_compile_definitions(SpaceCadetPinball PRIVATE SCP_NATIVE_MIDI)
endif()

target_precompile_headers(SpaceCadetPinball
        PUBLIC
        SpaceCadetPinball/pch.h
        )

target_link_libraries(SpaceCadetPinball imgui ${SDL_LIBRARIES})

# Read sources as UTF-8 and keep string literals in UTF-8; MSVC otherwise uses the system code page
if(MSVC)
    target_compile_options(SpaceCadetPinball PRIVATE /utf-8)
    target_compile_options(imgui PRIVATE /utf-8)
endif()

# Only the game target gets the extra warnings, not vendored SDL and ImGui.
# Unused parameters are allowed: overrides and callbacks have the signatures their interfaces set.
if(SCP_WARNINGS)
    if(MSVC)
        target_compile_options(SpaceCadetPinball PRIVATE /W4 /wd4100)
    else()
        target_compile_options(SpaceCadetPinball PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
    set_target_properties(SpaceCadetPinball PROPERTIES COMPILE_WARNING_AS_ERROR ON)
endif()

if(SCP_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "SCP_SANITIZERS needs GCC or Clang")
    endif()
    set(SCP_SANITIZER_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    # All three targets are instrumented; only the executable is linked (a static library has no link step)
    foreach(target SpaceCadetPinball imgui tinysoundfont)
        target_compile_options(${target} PRIVATE ${SCP_SANITIZER_FLAGS})
    endforeach()
    target_link_options(SpaceCadetPinball PRIVATE ${SCP_SANITIZER_FLAGS})
endif()

# On Windows with installed SDL packages, copy the DLLs next to the executable
if(WIN32 AND NOT SCP_VENDORED_SDL)
    add_custom_command(TARGET SpaceCadetPinball POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:SpaceCadetPinball> $<TARGET_FILE_DIR:SpaceCadetPinball>
        COMMAND_EXPAND_LISTS
        )
endif()

if(UNIX AND NOT APPLE)
	include(GNUInstallDirs)
	install(TARGETS "${PROJECT_NAME}" RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
	install(FILES "${CMAKE_SOURCE_DIR}/Platform/Linux/${PROJECT_NAME}.desktop" DESTINATION "share/applications")
	install(FILES "${CMAKE_SOURCE_DIR}/Platform/Linux/${PROJECT_NAME}.metainfo.xml" DESTINATION "share/metainfo")
	foreach(S 16 32 48 128 192)
		install(FILES "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}/Icon_${S}x${S}.png" DESTINATION
			"share/icons/hicolor/${S}x${S}/apps" RENAME "${PROJECT_NAME}.png")
	endforeach(S)
endif()
