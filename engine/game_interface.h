/**
 * :===============: ORIGIN ENGINE :===============:
 *   This is the game interface for Origin Engine.
 *             Include it in your game.
 */
#ifndef GAME_INTERFACE_H
#define GAME_INTERFACE_H

#include "vec3.h"
#include "entity.h"
#include "brush.h"
#include "trigger.h"
#include "console.h"
#include "player.h"
#include "brush_entity.h"
#include "map_format.h"
#include "entity_io.h"

/* == Engine API: functions the engine gives to the game ======== */

typedef struct {
    /* Entity system */
    Entity* (*entity_spawn)(void);
    void    (*entity_remove)(Entity* ent);
    Entity* (*entity_find_by_name)(const char* name);
    Entity* (*entity_find_by_class)(const char* classname);
    Entity* (*entity_get)(int id);
    int     (*entity_count)(void);
    Entity**(*entity_get_all)(int* out_count);

    /* Brush system */
    Brush*  (*brush_create)(Vec3 mins, Vec3 maxs, const char* texture, int solid);
    void    (*brush_set_face_texture)(Brush* b, int face, const char* texture);
    void    (*brush_set_face_scale)(Brush* b, int face, float u, float v);
    void    (*brush_set_face_offset)(Brush* b, int face, float u, float v);
    int     (*brush_count)(void);

    /* Trigger system */
    Trigger* (*trigger_create)(Vec3 mins, Vec3 maxs, int type, const char* name);
    int      (*trigger_count)(void);

    /* Console */
    CVar*   (*cvar_register)(const char* name, float default_val);
    CVar*   (*cvar_find)(const char* name);
    float   (*cvar_get)(const char* name);
    void    (*cvar_set)(const char* name, float value);
    void    (*cmd_register)(const char* name, CmdFunc func);
    void    (*cmd_execute)(const char* input);

    /* Player */
    Entity* (*player_spawn)(Vec3 pos);
    Entity* (*player_get)(void);
    PlayerData* (*player_data)(Entity* ent);

    /* Brush entities */
    Entity* (*brush_entity_create)(const char* classname, const char* targetname);
    Brush*  (*brush_entity_add_brush)(Entity* ent, Vec3 mins, Vec3 maxs, const char* texture, int solid);
    BrushEntData* (*brush_entity_data)(Entity* ent);

    /* Map I/O */
    int     (*map_load)(const char* path, MapInfo* out_info);
    int     (*map_save)(const char* path, const MapInfo* info);

    /* Entity I/O */
    EntityIO* (*entity_io_get)(Entity* ent);
    void    (*entity_io_connect)(Entity* ent, const char* output,
                                 const char* target, const char* input,
                                 const char* parameter, float delay, int once);
    void    (*entity_io_fire_output)(Entity* ent, const char* output, Entity* activator);
    void    (*entity_io_register_input)(const char* classname, const char* input_name, InputFunc func);
    void    (*entity_io_send_input)(Entity* target, const char* input_name,
                                    Entity* activator, const char* parameter);

    /* Sound */
    int     (*sound_play)(const char* filename);
    int     (*sound_play_vol)(const char* filename, float volume);
    int     (*sound_play_random)(const char* prefix, int count, const char* suffix);

    /* Props */
    int     (*prop_add)(const char* mesh_name, Vec3 origin, Vec3 angles, float scale);
    int     (*prop_add_physics)(const char* mesh_name, Vec3 origin, Vec3 angles, float scale, float mass);

    /* Skybox */
    void    (*render_set_skybox)(const char* texture_path);

    /* Logging */
    void    (*log)(const char* fmt, ...);
} EngineAPI;

/* == Game API: functions the game .so must export ============== */

typedef struct {
    /* Called once when the game is loaded. Engine API is passed in. */
    int     (*game_init)(const EngineAPI* engine);

    /* Called once when shutting down. */
    void    (*game_shutdown)(void);

    /* Called every frame with delta time. */
    void    (*game_frame)(float dt);

    /* Called to build/load the map. */
    void    (*game_load_map)(const char* map_name);

    /* Game name / version for logging */
    const char* (*game_name)(void);
    const char* (*game_version)(void);
} GameAPI;

/*
 * The game .so must export this function:
 *   GameAPI* game_get_api(void);
 *
 * The engine calls it after dlopen to get the game's function table.
 */
typedef GameAPI* (*GetGameAPIFunc)(void);

#define GAME_API_EXPORT __attribute__((visibility("default")))

#endif /* GAME_INTERFACE_H */
