/*
 * sound.c — Sound system implementation using miniaudio + stb_vorbis (OGG)
 */
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/* Full stb_vorbis implementation after miniaudio */
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SOUNDS 32

static ma_engine  g_engine;
static int        g_initialized = 0;
static char       g_base_path[256] = "";
static ma_sound   g_sounds[MAX_SOUNDS];
static int        g_sound_used[MAX_SOUNDS];
static int        g_rng_seeded = 0;

int sound_init(void) {
    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &g_engine) != MA_SUCCESS) {
        printf("[sound] failed to init audio engine\n");
        return 0;
    }
    memset(g_sound_used, 0, sizeof(g_sound_used));
    g_initialized = 1;
    if (!g_rng_seeded) { srand((unsigned)time(NULL)); g_rng_seeded = 1; }
    printf("[sound] initialized\n");
    return 1;
}

void sound_shutdown(void) {
    if (!g_initialized) return;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_sound_used[i]) {
            ma_sound_uninit(&g_sounds[i]);
            g_sound_used[i] = 0;
        }
    }
    ma_engine_uninit(&g_engine);
    g_initialized = 0;
    printf("[sound] shutdown\n");
}

void sound_set_base_path(const char* path) {
    strncpy(g_base_path, path, sizeof(g_base_path) - 1);
}

static int find_free_slot(void) {
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_sound_used[i] && !ma_sound_is_playing(&g_sounds[i])) {
            ma_sound_uninit(&g_sounds[i]);
            g_sound_used[i] = 0;
            return i;
        }
    }
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (!g_sound_used[i]) return i;
    }
    return -1;
}

int sound_play(const char* filename) {
    return sound_play_vol(filename, 1.0f);
}

int sound_play_vol(const char* filename, float volume) {
    if (!g_initialized) return 0;

    char path[512];
    if (g_base_path[0])
        snprintf(path, sizeof(path), "%s/%s", g_base_path, filename);
    else
        strncpy(path, filename, sizeof(path) - 1);

    int slot = find_free_slot();
    if (slot < 0) return 0;

    if (ma_sound_init_from_file(&g_engine, path, 0, NULL, NULL, &g_sounds[slot]) != MA_SUCCESS) {
        printf("[sound] can't load: %s\n", path);
        return 0;
    }

    ma_sound_set_volume(&g_sounds[slot], volume);
    ma_sound_start(&g_sounds[slot]);
    g_sound_used[slot] = 1;
    return 1;
}

int sound_play_random(const char* prefix, int count, const char* suffix) {
    if (count <= 0) return 0;
    int n = (rand() % count) + 1;
    char filename[128];
    snprintf(filename, sizeof(filename), "%s%d%s", prefix, n, suffix);
    return sound_play(filename);
}

void sound_stop_all(void) {
    if (!g_initialized) return;
    for (int i = 0; i < MAX_SOUNDS; i++) {
        if (g_sound_used[i]) {
            ma_sound_stop(&g_sounds[i]);
            ma_sound_uninit(&g_sounds[i]);
            g_sound_used[i] = 0;
        }
    }
}
