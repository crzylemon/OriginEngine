/*
 * game_dll.c — Game shared library loader (cross-platform)
 */
#include "game_dll.h"
#include "map_format.h"
#include "sound.h"
#include "prop.h"
#include "render.h"
#include <stdio.h>
#include <stdarg.h>

#ifdef _WIN32
  #include <windows.h>
  typedef HMODULE dll_handle_t;
  #define DLL_OPEN(path)       LoadLibraryA(path)
  #define DLL_SYM(h, name)    ((void*)GetProcAddress(h, name))
  #define DLL_CLOSE(h)         FreeLibrary(h)
  static const char* dll_error(void) {
      static char buf[256];
      FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(),
                     0, buf, sizeof(buf), NULL);
      return buf;
  }
#else
  #include <dlfcn.h>
  typedef void* dll_handle_t;
  #define DLL_OPEN(path)       dlopen(path, RTLD_NOW)
  #define DLL_SYM(h, name)    dlsym(h, name)
  #define DLL_CLOSE(h)         dlclose(h)
  #define dll_error()          dlerror()
#endif

static dll_handle_t g_dll_handle = NULL;
static GameAPI*     g_game_api = NULL;
static EngineAPI    g_engine_api;

/* Engine log function */
static void engine_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[game] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

/* Build the engine API table with real function pointers */
static void build_engine_api(void) {
    g_engine_api.entity_spawn        = entity_spawn;
    g_engine_api.entity_remove       = entity_remove;
    g_engine_api.entity_find_by_name = entity_find_by_name;
    g_engine_api.entity_find_by_class= entity_find_by_class;
    g_engine_api.entity_get          = entity_get;
    g_engine_api.entity_count        = entity_count;
    g_engine_api.entity_get_all      = entity_get_all;

    g_engine_api.brush_create        = brush_create;
    g_engine_api.brush_set_face_texture = brush_set_face_texture;
    g_engine_api.brush_set_face_scale   = brush_set_face_scale;
    g_engine_api.brush_set_face_offset  = brush_set_face_offset;
    g_engine_api.brush_count         = brush_count;

    g_engine_api.trigger_create      = trigger_create;
    g_engine_api.trigger_count       = trigger_count;

    g_engine_api.cvar_register       = cvar_register;
    g_engine_api.cvar_find           = cvar_find;
    g_engine_api.cvar_get            = cvar_get;
    g_engine_api.cvar_set            = cvar_set;
    g_engine_api.cmd_register        = cmd_register;
    g_engine_api.cmd_execute         = cmd_execute;

    g_engine_api.player_spawn        = player_spawn;
    g_engine_api.player_get          = player_get;
    g_engine_api.player_data         = player_data;

    g_engine_api.brush_entity_create    = brush_entity_create;
    g_engine_api.brush_entity_add_brush = brush_entity_add_brush;
    g_engine_api.brush_entity_data      = brush_entity_data;

    g_engine_api.map_load            = map_load;
    g_engine_api.map_save            = map_save;

    g_engine_api.entity_io_get           = entity_io_get;
    g_engine_api.entity_io_connect       = entity_io_connect;
    g_engine_api.entity_io_fire_output   = entity_io_fire_output;
    g_engine_api.entity_io_register_input= entity_io_register_input;
    g_engine_api.entity_io_send_input    = entity_io_send_input;

    g_engine_api.sound_play              = sound_play;
    g_engine_api.sound_play_vol          = sound_play_vol;
    g_engine_api.sound_play_random       = sound_play_random;

    g_engine_api.prop_add                = prop_add;
    g_engine_api.prop_add_physics        = prop_add_physics;

    g_engine_api.render_set_skybox       = render_set_skybox;
    g_engine_api.log                     = engine_log;
    g_engine_api.game_folder             = NULL;
}

int game_dll_load(const char* path, const char* game_folder) {
    printf("[engine] loading game: %s\n", path);

    g_dll_handle = DLL_OPEN(path);
    if (!g_dll_handle) {
        printf("[engine] dll load failed: %s\n", dll_error());
        return 0;
    }

    GetGameAPIFunc get_api = (GetGameAPIFunc)DLL_SYM(g_dll_handle, "game_get_api");
    if (!get_api) {
        printf("[engine] game_get_api not found: %s\n", dll_error());
        DLL_CLOSE(g_dll_handle);
        g_dll_handle = NULL;
        return 0;
    }

    g_game_api = get_api();
    if (!g_game_api) {
        printf("[engine] game_get_api returned NULL\n");
        DLL_CLOSE(g_dll_handle);
        g_dll_handle = NULL;
        return 0;
    }

    build_engine_api();
    g_engine_api.game_folder = game_folder;

    printf("[engine] loaded: %s v%s\n",
           g_game_api->game_name ? g_game_api->game_name() : "unknown",
           g_game_api->game_version ? g_game_api->game_version() : "?");

    if (g_game_api->game_init) {
        if (!g_game_api->game_init(&g_engine_api)) {
            printf("[engine] game_init failed\n");
            DLL_CLOSE(g_dll_handle);
            g_dll_handle = NULL;
            g_game_api = NULL;
            return 0;
        }
    }

    return 1;
}

GameAPI* game_dll_get_api(void) {
    return g_game_api;
}

EngineAPI* game_dll_get_engine_api(void) {
    return &g_engine_api;
}

void game_dll_unload(void) {
    if (g_game_api && g_game_api->game_shutdown)
        g_game_api->game_shutdown();
    if (g_dll_handle) {
        DLL_CLOSE(g_dll_handle);
        printf("[engine] game unloaded\n");
    }
    g_dll_handle = NULL;
    g_game_api = NULL;
}
