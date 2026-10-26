# FCEUX with the Mega Man 3 Remade album built in

This is FCEUX (Qt/SDL build) with the companion player and the sound-hook Lua script folded into the
emulator itself. No Lua script and no separate program: FCEUX watches the game's sound requests and plays
the album's WAV files through its own audio output.

## Using it

1. Use the ROM that has the original music muted (`MM3_Revamped_1_1_remade_audio.nes`, or the
   `..._audio_options.nes` one). With the plain ROM you would hear the NES music and the album together.
2. In FCEUX's base folder make a folder called `mm3_remade` and put the album WAV files in it.
   (On Windows the base folder is the folder with the FCEUX .exe. On Linux it is `~/.fceux`; the
   environment variables FCEUX_HOME / FCEUX_CONFIG_DIR change it.)
3. Open the ROM. That is all. The FCEUX message log says
   `MM3 audio: 31 album tracks found in ...` when it is active.

File names: `01.wav` ... `31.wav` are what the folder needs; `... - 01 Logos.wav` style names from the album
work too. WAVs may be 8/16/24/32-bit PCM or 32-bit float, mono or stereo, any sample rate (they are
resampled on the fly to whatever FCEUX's sound rate is).

Optional files in the same folder (this repo ships examples in `output/mm3_remade/`):

| file | purpose |
|---|---|
| `config.txt` | `volume=0.8`, `enabled=1`, `preload=1`, `verbose=0` |
| `map.csv` | which game sound ID (and stage) plays which track. Same format as the companion's file. |
| `loops.csv` | loop start/end seconds per track. Same format as the companion's file. |

If `map.csv` / `loops.csv` are missing, the built-in copies (the ones you were using) apply.

## Behaviour

* The album pauses with FCEUX's pause, mutes with FCEUX's mute, and stands still during fast-forward/turbo.
* Reset and loading a savestate stop the album until the game's next music request (the game state does not
  tell the emulator which song a savestate was playing).
* Only a game that looks like Mega Man 3 activates it (MMC3 mapper and the three sound-driver call sites at
  $FFBC, $C107 and $CBFD). Every other ROM is untouched.
* The Wily 4 Boss Rush events (A0/A1) from the Lua script are done natively, as are the stop codes F0/F2.
* Output is now stereo (the emulator's own sound is centred; the album keeps its stereo image).
* The album is held in memory decoded (about 0.5 GB for the whole album). `preload=0` loads tracks on demand.

## Building

Source changes: `src/mm3audio.cpp/.h/.inc` (new), small hooks in `src/fceu.cpp`, `src/state.cpp`,
`src/CMakeLists.txt`, and the audio callback in `src/drivers/Qt/sdl-sound.cpp`.
`mm3-remade-audio.patch` contains exactly these changes against the FCEUX source you sent.

* Windows, on your own PC: install Visual Studio 2022 (C++ workload), Qt 6.9 (msvc2022_64) in `C:\Qt\6.9`,
  Git, then run `pipelines\qwin64_build.bat` from a normal command prompt in this folder. The finished
  program is in `build\bin` (`qfceux.exe`). Put `mm3_remade` next to it.
* Windows, without installing anything: put this source on GitHub and run the "Windows (Qt) build" workflow
  (`.github/workflows/windows-mm3.yml`); download the artifact.
* Linux/macOS: the normal FCEUX CMake build (`pipelines/linux_build.sh`), nothing extra needed.

The legacy Win32 front-end (`vc14_fceux.sln`) is not changed: it has a different audio driver.
