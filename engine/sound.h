/*
 * sound.h — Sound system for Origin Engine (powered by miniaudio)
 *
 * Supports WAV and OGG playback, multiple simultaneous sounds.
 */
#ifndef SOUND_H
#define SOUND_H

/* Init/shutdown — call once */
int  sound_init(void);
void sound_shutdown(void);

/* Set the base path for sound files (e.g. "game/sound") */
void sound_set_base_path(const char* path);

/* Play a sound file (relative to base path). Returns 0 on failure. */
int  sound_play(const char* filename);

/* Play with volume (0.0 - 1.0) */
int  sound_play_vol(const char* filename, float volume);

/* Play a random sound from a set: prefix + random number 1..count + suffix
   e.g. sound_play_random("grassSand", 6, ".ogg") plays grassSand1.ogg..grassSand6.ogg */
int  sound_play_random(const char* prefix, int count, const char* suffix);

/* Stop all currently playing sounds */
void sound_stop_all(void);

#endif /* SOUND_H */
