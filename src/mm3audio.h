/* Mega Man 3 "Remade" album player (built into FCEUX).
 *
 * Plays WAV files from <FCEUX base folder>/mm3_remade/ in place of the game's music
 * by watching the game's calls to its sound driver (what mm3_sound_hook.lua used to
 * report to the separate companion program).  Mixed into the emulator's own audio
 * output, so it pauses, mutes and stops together with the emulator.
 */
#ifndef MM3AUDIO_H
#define MM3AUDIO_H

#include <stdint.h>

/// Call after a game has been loaded (detects the ROM and loads the album).
void MM3Audio_GameLoaded(void);
/// Call when the game is closed.
void MM3Audio_GameClosed(void);
/// Stop the album track (reset, savestate load, rewind).
void MM3Audio_Stop(void);
/// Mix the album into an interleaved 16-bit stereo buffer (audio thread).
/// silent != 0: emulator paused/muted/fast-forwarding; adds nothing and does not advance.
void MM3Audio_Mix(int16_t *stereo, int frames, int outRate, int silent);
/// Nonzero while the album is active for the loaded game.
int MM3Audio_Active(void);

#endif
