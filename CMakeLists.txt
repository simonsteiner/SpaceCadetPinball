cmake_minimum_required(VERSION 3.24)
project(SpaceCadetPinball)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeModules")

# Windows and macOS have no standard SDL packages, so build SDL from source there by default
if(WIN32 OR APPLE)
    set(SCP_VENDORED_SDL_DEFAULT ON)
else()
    set(SCP_VENDORED_SDL_DEFAULT OFF)
endif()
option(SCP_VENDORED_SDL "Build SDL2 and SDL2_mixer from pinned sources instead of using installed packages" ${SCP_VENDORED_SDL_DEFAULT})
option(SCP_WARNINGS "Compile the game with extra warnings (-Wall -Wextra, or /W4 with MSVC) and treat them as errors" OFF)
option(SCP_SANITIZERS "Build the game with AddressSanitizer and UndefinedBehaviorSanitizer (GCC and Clang)" OFF)

# Link mingw libs static
if(MINGW)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
endif()

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.11")
endif()

if(SCP_VENDORED_SDL)
    include(FetchContent)

    # Let option() in SDL2, which declares an older CMake version, honour the variables below
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

    # Static libraries, nothing installed, no SDL2main
    set(BUILD_SHARED_LIBS OFF)
    set(SDL_SHARED OFF)
    set(SDL_STATIC ON)
    set(SDL_TEST OFF)
    set(SDL2_DISABLE_SDL2MAIN ON)
    set(SDL2_DISABLE_INSTALL ON)
    set(SDL2_DISABLE_UNINSTALL ON)
    # Off, or SDL raises its minimum CMake version below 3.10 and compiles through ccache when installed
    set(SDL_CCACHE OFF)

    # The game only plays WAV sounds and MIDI music: Timidity, plus native MIDI on Windows and macOS
    set(SDL2MIXER_INSTALL OFF)
    set(SDL2MIXER_SAMPLES OFF)
    set(SDL2MIXER_VENDORED OFF)
    set(SDL2MIXER_CMD OFF)
    set(SDL2MIXER_FLAC OFF)
    set(SDL2MIXER_GME OFF)
    set(SDL2MIXER_MOD OFF)
    set(SDL2MIXER_MP3 OFF)
    set(SDL2MIXER_OPUS OFF)
    set(SDL2MIXER_VORBIS OFF CACHE STRING "Enable OGG Vorbis music")
    set(SDL2MIXER_WAVPACK OFF)
    set(SDL2MIXER_WAVE ON)
    set(SDL2MIXER_MIDI ON)
    set(SDL2MIXER_MIDI_FLUIDSYNTH OFF)
    set(SDL2MIXER_MIDI_TIMIDITY ON)
    set(SDL2MIXER_MIDI_NATIVE ON)

    FetchContent_Declare(SDL2
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
            URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165
            )
    FetchContent_Declare(SDL2_mixer
            URL https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.1/SDL2_mixer-2.8.1.tar.gz
            URL_HASH SHA256=cb760211b056bfe44f4a1e180cc7cb201137e4d1572f2002cc1be728efd22660
            )
    FetchContent_MakeAvailable(SDL2 SDL2_mixer)

    set(SDL_LIBRARIES SDL2_mixer::SDL2_mixer-static SDL2::SDL2-static)
else()
    # SDL2main is not needed
    set(SDL2_BUILDING_LIBRARY ON)

    find_package(SDL2 REQUIRED)
    FIND_PACKAGE(SDL2_mixer REQUIRED)

    include_directories(${SDL2_INCLUDE_DIR} ${SDL2_MIXER_INCLUDE_DIR})
    get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
    foreach(dir ${dirs})
      message(STATUS "Include dir='${dir}'")
    endforeach()

    set(SDL_LIBRARIES ${SDL2_LIBRARY} ${SDL2_MIXER_LIBRARY})
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
        SpaceCadetPinball/proj.cpp
        SpaceCadetPinball/proj.h
        SpaceCadetPinball/render.cpp
        SpaceCadetPinball/render.h
        SpaceCadetPinball/replay.cpp
        SpaceCadetPinball/replay.h
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
        SpaceCadetPinball/imgui_impl_sdl2.cpp
        SpaceCadetPinball/imgui_impl_sdl2.h
        SpaceCadetPinball/imgui_impl_sdlrenderer2.cpp
        SpaceCadetPinball/imgui_impl_sdlrenderer2.h
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

add_executable(SpaceCadetPinball ${SOURCE_FILES})

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
    # Both targets are instrumented; only the executable is linked (a static library has no link step)
    foreach(target SpaceCadetPinball imgui)
        target_compile_options(${target} PRIVATE ${SCP_SANITIZER_FLAGS})
    endforeach()
    target_link_options(SpaceCadetPinball PRIVATE ${SCP_SANITIZER_FLAGS})
endif()

# On Windows with installed SDL packages, copy DLL to output
if(WIN32 AND NOT SCP_VENDORED_SDL)
    list(GET SDL2_LIBRARY -1 SDL2_DLL_PATH)
    list(GET SDL2_MIXER_LIBRARY -1 SDL2_MIXER_DLL_PATH)
    get_filename_component(SDL2_DLL_PATH ${SDL2_DLL_PATH} DIRECTORY)
    get_filename_component(SDL2_MIXER_DLL_PATH ${SDL2_MIXER_DLL_PATH} DIRECTORY)
    if(MINGW)
        string(REGEX REPLACE "lib$" "bin" SDL2_DLL_PATH ${SDL2_DLL_PATH})
        string(REGEX REPLACE "lib$" "bin" SDL2_MIXER_DLL_PATH ${SDL2_MIXER_DLL_PATH})
    endif()
    message(STATUS "copy paths='${SDL2_DLL_PATH}' '${SDL2_MIXER_DLL_PATH}'") 
    add_custom_command(TARGET SpaceCadetPinball POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_DLL_PATH}/SDL2.dll" $<TARGET_FILE_DIR:SpaceCadetPinball>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDL2_MIXER_DLL_PATH}/SDL2_mixer.dll" $<TARGET_FILE_DIR:SpaceCadetPinball>
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
