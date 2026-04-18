/*
 * game.c — Origin Demo Game
 *
 * Compiled as game.so, loaded by the engine at runtime.
 * Handles map loading, entity behavior, and I/O wiring.
 */
#include "../engine/game_interface.h"
#include "../engine/map_format.h"
#include <stdio.h>
#include <string.h>

static const EngineAPI* eng = NULL;

/* ═══════════════════════════════════════════════════════════════
 *  Entity Input Handlers (Source-style)
 * ═══════════════════════════════════════════════════════════════ */

/* Door states stored in entity health field (repurposed as state) */
#define DOOR_CLOSED  0
#define DOOR_OPENING 1
#define DOOR_OPEN    2
#define DOOR_CLOSING 3

/* func_door inputs */
static void door_open(Entity* self, Entity* activator, const char* param) {
    (void)param; (void)activator;
    if (self->max_health != DOOR_CLOSED) return;
    eng->log("door '%s' opening", self->targetname);
    eng->sound_play("office_door.wav");
    self->velocity.z = 100.0f;
    self->next_think = 1.5f;
    self->max_health = DOOR_OPENING;
}

static void door_close(Entity* self, Entity* activator, const char* param) {
    (void)activator; (void)param;
    if (self->max_health != DOOR_OPEN) return;
    eng->log("door '%s' closing", self->targetname);
    eng->sound_play("office_door.wav");
    self->velocity.z = -100.0f;
    self->next_think = 1.5f;
    self->max_health = DOOR_CLOSING;
}

static void door_toggle(Entity* self, Entity* activator, const char* param) {
    int state = self->max_health;
    if (state == DOOR_CLOSED || state == DOOR_CLOSING)
        door_open(self, activator, param);
    else
        door_close(self, activator, param);
}

static void door_think(Entity* self) {
    self->velocity = VEC3_ZERO;
    if (self->max_health == DOOR_OPENING) {
        self->max_health = DOOR_OPEN;
        eng->entity_io_fire_output(self, "OnFullyOpen", NULL);
    } else if (self->max_health == DOOR_CLOSING) {
        self->max_health = DOOR_CLOSED;
        eng->entity_io_fire_output(self, "OnFullyClosed", NULL);
    }
}

/* Trap door: slides sideways */
static void door_slide_open(Entity* self, Entity* activator, const char* param) {
    (void)param; (void)activator;
    if (self->max_health != DOOR_CLOSED) return;
    eng->log("trap '%s' sliding open", self->targetname);
    eng->sound_play("trapActive.wav");
    self->velocity.x = 200.0f;
    self->next_think = 1.5f;
    self->max_health = DOOR_OPENING;
}

/* logic_relay inputs */
static void relay_trigger(Entity* self, Entity* activator, const char* param) {
    (void)param;
    eng->log("relay '%s' triggered", self->targetname);
    eng->entity_io_fire_output(self, "OnTrigger", activator);
}

/* trigger_hurt: damages entities inside it */
static void trigger_hurt_think(Entity* self) {
    /* Find player and check if inside our bounds */
    Entity* pe = eng->player_get();
    if (!pe) return;
    BrushEntData* bd = eng->brush_entity_data(self);
    if (!bd || bd->brush_count == 0) return;

    /* Compute world bounds */
    Brush* b = bd->brushes[0];
    Vec3 off = vec3_sub(self->origin, bd->spawn_origin);
    Vec3 mins = vec3_add(b->mins, off);
    Vec3 maxs = vec3_add(b->maxs, off);

    Vec3 p = pe->origin;
    if (p.x >= mins.x && p.x <= maxs.x &&
        p.y >= mins.y && p.y <= maxs.y &&
        p.z >= mins.z && p.z <= maxs.z) {
        /* Inside — deal damage */
        int dmg = 10; /* default damage per tick */
        pe->health -= dmg;
        if (pe->health < 0) pe->health = 0;
        eng->entity_io_fire_output(self, "OnHurt", pe);
        if (pe->health <= 0)
            eng->entity_io_fire_output(self, "OnHurtKill", pe);
    }

    /* Re-schedule think (damage every 0.5s) */
    self->next_think = 0.5f;
}

/* func_button: press Use to activate */
static void button_use(Entity* self, Entity* activator) {
    eng->log("button '%s' pressed", self->targetname);
    eng->entity_io_fire_output(self, "OnPressed", activator);
}

/* logic_auto: fires OnMapSpawn once at map load */
static int g_logic_auto_fired = 0;

/* Custom trigger callback — fires entity I/O instead of Use */
static void trigger_io_callback(Trigger* trig, Entity* activator) {
    /* Find the trigger's owner entity by name and fire its output */
    Entity* ent = eng->entity_find_by_name(trig->name);
    if (ent) {
        eng->entity_io_fire_output(ent, "OnTrigger", activator);
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Register all entity classes and their inputs
 * ═══════════════════════════════════════════════════════════════ */

static void register_entity_classes(void) {
    /* func_door */
    eng->entity_io_register_input("func_door", "Open", door_open);
    eng->entity_io_register_input("func_door", "Close", door_close);
    eng->entity_io_register_input("func_door", "Toggle", door_toggle);
    eng->entity_io_register_input("func_door", "SlideOpen", door_slide_open);

    /* logic_relay */
    eng->entity_io_register_input("logic_relay", "Trigger", relay_trigger);

    /* func_button uses the Use callback, no special inputs needed */

    eng->log("registered entity classes");
}

/* ═══════════════════════════════════════════════════════════════
 *  Wire up entities after map load (set callbacks, I/O, etc.)
 * ═══════════════════════════════════════════════════════════════ */

static void wire_entities(void) {
    int count;
    Entity** ents = eng->entity_get_all(&count);

    for (int i = 0; i < count; i++) {
        Entity* e = ents[i];
        if (!e) continue;

        /* func_door: set think callback */
        if (strcmp(e->classname, "func_door") == 0) {
            e->think = door_think;
            e->max_health = DOOR_CLOSED; /* init door state */
        }

        /* trigger_hurt: set up damage think */
        if (strcmp(e->classname, "trigger_hurt") == 0) {
            e->think = trigger_hurt_think;
            e->next_think = 0.5f;
        }

        /* func_button: wire Use callback */
        if (strcmp(e->classname, "func_button") == 0) {
            e->use = button_use;
        }

        /* logic_auto: fire OnMapSpawn once */
        if (strcmp(e->classname, "logic_auto") == 0 && !g_logic_auto_fired) {
            g_logic_auto_fired = 1;
            eng->entity_io_fire_output(e, "OnMapSpawn", NULL);
        }

        /* trigger_once / trigger_multiple: create Trigger volume from brush bounds */
        if (strncmp(e->classname, "trigger_", 8) == 0) {
            BrushEntData* bd = eng->brush_entity_data(e);
            if (bd && bd->brush_count > 0) {
                /* Compute AABB from all brush entity brushes */
                Vec3 mins = VEC3(1e9f, 1e9f, 1e9f);
                Vec3 maxs = VEC3(-1e9f, -1e9f, -1e9f);
                /* Compute world-space AABB from brush vertices + entity origin */
                for (int j = 0; j < bd->brush_count; j++) {
                    Brush* b = bd->brushes[j];
                    if (!b) continue;
                    for (int f = 0; f < b->face_count; f++) {
                        for (int v = 0; v < b->faces[f].vertex_count; v++) {
                            Vec3 p = vec3_add(b->faces[f].vertices[v], e->origin);
                            if (p.x < mins.x) mins.x = p.x;
                            if (p.y < mins.y) mins.y = p.y;
                            if (p.z < mins.z) mins.z = p.z;
                            if (p.x > maxs.x) maxs.x = p.x;
                            if (p.y > maxs.y) maxs.y = p.y;
                            if (p.z > maxs.z) maxs.z = p.z;
                        }
                    }
                }

                int type = (strcmp(e->classname, "trigger_once") == 0) ? TRIG_ONCE : TRIG_MULTIPLE;
                Trigger* t = eng->trigger_create(mins, maxs, type, e->targetname);
                t->on_trigger = trigger_io_callback;
                eng->log("created trigger '%s' at (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)",
                         e->targetname, mins.x, mins.y, mins.z, maxs.x, maxs.y, maxs.z);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  Game API
 * ═══════════════════════════════════════════════════════════════ */

static int my_game_init(const EngineAPI* engine) {
    eng = engine;
    eng->log("game initialized");
    eng->cvar_register("sv_gravity", 800.0f);
    register_entity_classes();
    return 1;
}

static void my_game_shutdown(void) {
    if (eng) eng->log("game shutting down");
}

static void cmd_savemap(const char* args) {
    const char* filename = args && args[0] ? args : "saved.oem";
    char save_path[256];
    snprintf(save_path, sizeof(save_path), "%s/maps/%s", eng->game_folder, filename);
    MapInfo info;
    snprintf(info.title, sizeof(info.title), "Saved Map");
    snprintf(info.description, sizeof(info.description), "Map saved from Origin Engine");
    eng->map_save(save_path, &info);
}

static void build_default_map(void) {
    eng->log("building default map in code");

    Brush* b;

    /* Floor */
    b = eng->brush_create(VEC3(-512,-512,-16), VEC3(512,512,0), "dirt", 1);
    eng->brush_set_face_texture(b, FACE_TOP, "grass");
    eng->brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);

    /* Walls */
    b = eng->brush_create(VEC3(-512,-512,0), VEC3(-496,512,128), "bricks", 1);
    eng->brush_set_face_texture(b, FACE_WEST, "nodraw");
    b = eng->brush_create(VEC3(496,-512,0), VEC3(512,512,128), "bricks", 1);
    eng->brush_set_face_texture(b, FACE_EAST, "nodraw");
    b = eng->brush_create(VEC3(-512,-512,0), VEC3(512,-496,128), "bricks", 1);
    eng->brush_set_face_texture(b, FACE_SOUTH, "nodraw");
    b = eng->brush_create(VEC3(-512,496,0), VEC3(512,512,128), "bricks", 1);
    eng->brush_set_face_texture(b, FACE_NORTH, "nodraw");

    /* Pillars */
    eng->brush_create(VEC3(-100,-100,0), VEC3(-80,-80,200), "plywood", 1);
    eng->brush_create(VEC3(80,-100,0), VEC3(100,-80,200), "plywood", 1);
    eng->brush_create(VEC3(-100,80,0), VEC3(-80,100,200), "plywood", 1);
    eng->brush_create(VEC3(80,80,0), VEC3(100,100,200), "plywood", 1);

    /* Ceiling */
    b = eng->brush_create(VEC3(-200,-200,128), VEC3(200,200,140), "plywood", 1);
    eng->brush_set_face_texture(b, FACE_BOTTOM, "boards_straight_small");

    /* Ramp */
    b = eng->brush_create(VEC3(200,-50,0), VEC3(350,50,60), "plywood", 1);
    eng->brush_set_face_texture(b, FACE_TOP, "boards_straight");

    /* ── Door (brush entity) ─────────────────────────────────── */
    Entity* door = eng->brush_entity_create("func_door", "main_door");
    door->origin = VEC3(300, 200, 0);
    eng->brush_entity_data(door)->spawn_origin = door->origin;
    Brush* db = eng->brush_entity_add_brush(door,
        VEC3(-20,-40,0), VEC3(20,40,100), "plywood", 1);
    eng->brush_set_face_texture(db, FACE_EAST, "boards_straight");
    eng->brush_set_face_texture(db, FACE_WEST, "boards_straight");

    /* ── Trigger -> Door I/O ─────────────────────────────────── */

    /* Create a trigger entity so I/O can reference it */
    Entity* trig_ent = eng->entity_spawn();
    strcpy(trig_ent->classname, "trigger_once");
    strcpy(trig_ent->targetname, "door_trigger");
    trig_ent->origin = VEC3(300, 200, 36);

    /* Wire I/O: trigger fires -> door opens, then after 3s -> door closes */
    eng->entity_io_connect(trig_ent, "OnTrigger", "main_door", "Open", "", 0, 0);
    eng->entity_io_connect(trig_ent, "OnTrigger", "main_door", "Close", "", 3.0f, 0);

    /* Create the actual trigger volume */
    Trigger* t = eng->trigger_create(VEC3(250,150,0), VEC3(350,250,72),
                                      TRIG_ONCE, "door_trigger");
    t->on_trigger = trigger_io_callback;

    /* Player spawn point */
    Entity* player = eng->entity_spawn();
    strcpy(player->classname, "info_player_start");
    strcpy(player->targetname, "");
    player->origin = VEC3(0, -100, 1);
    player->flags = 0;

    /* Visible trigger brush */
    eng->brush_create(VEC3(250,150,0), VEC3(350,250,72), "nodraw", 0);

    /* ── Logic relay example ─────────────────────────────────── */
    Entity* relay = eng->entity_spawn();
    strcpy(relay->classname, "logic_relay");
    strcpy(relay->targetname, "relay1");

    eng->player_spawn(VEC3(-100, 0, 1));
}

/* Place some props after map load */
static void place_props(void) {
    eng->render_set_skybox("textures/skybox_plains.png");
    eng->prop_add_physics("crate", VEC3(-200, 50, 20), VEC3(0,0,0), 1.0f, 5.0f);
    eng->prop_add_physics("crate", VEC3(-200, 50, 56), VEC3(15,0,0), 1.0f, 5.0f);
    eng->prop_add_physics("crate", VEC3(-170, 60, 20), VEC3(45,0,0), 1.0f, 5.0f);
    eng->prop_add_physics("barrel", VEC3(-80, -250, 20), VEC3(0,0,0), 1.0f, 3.0f);
    eng->prop_add_physics("barrel", VEC3(-50, -260, 20), VEC3(30,0,0), 1.0f, 3.0f);
    eng->prop_add_physics("barrel", VEC3(600, -100, 20), VEC3(0,0,0), 1.0f, 3.0f);
    eng->prop_add("lamp_post", VEC3(200, 200, 0), VEC3(0,0,0), 1.0f);
    eng->prop_add("lamp_post", VEC3(-200, 200, 0), VEC3(0,0,0), 1.0f);
}

static void my_game_load_map(const char* map_name) {
    if (!eng) return;
    eng->log("loading map: %s", map_name);

    eng->cmd_register("savemap", cmd_savemap);

    /* Try .oem file */
    char oem_path[256];
    snprintf(oem_path, sizeof(oem_path), "%s/maps/%s.oem", eng->game_folder, map_name);
    MapInfo info;
    if (eng->map_load(oem_path, &info)) {
        eng->log("loaded map file: %s", oem_path);
        Entity* spawn = eng->entity_find_by_class("info_player_start");
        if (spawn)
            eng->player_spawn(spawn->origin);
        else
            eng->player_spawn(VEC3(-100, 0, 1));
        wire_entities();
        place_props();
        return;
    }

    /* Fallback: build in code */
    build_default_map();
    wire_entities();
    place_props();
}

static float g_footstep_timer = 0;

static void my_game_frame(float dt) {
    Entity* pe = eng->player_get();
    if (!pe || pe->health <= 0) return;

    PlayerData* pd = eng->player_data(pe);
    if (!pd || pd->noclip) return;

    if (pd->on_ground) {
        float speed = vec3_len(VEC3(pe->velocity.x, pe->velocity.y, 0));
        if (speed > 10.0f) {
            g_footstep_timer -= dt;
            if (g_footstep_timer <= 0) {
                /* Check what texture we're standing on by raycasting down
                   For now, use position-based heuristic from the map layout */
                float x = pe->origin.x, y = pe->origin.y, z = pe->origin.z;

                if (z > 75 && x > -90 && x < 90 && y > 240) {
                    /* Metal platform */
                    eng->sound_play_random("metal", 6, ".wav");
                } else if (x > 500 && y > -200 && y < 200) {
                    /* Office (carpet — use stone as placeholder) */
                    eng->sound_play_random("stone", 6, ".ogg");
                } else {
                    /* Default outdoor */
                    eng->sound_play_random("grassSand", 6, ".ogg");
                }
                float interval = pd->crouching ? 0.55f : 0.35f;
                g_footstep_timer = interval;
            }
        } else {
            g_footstep_timer = 0;
        }
    }
}

static const char* my_game_name(void) { return "Origin Demo Game"; }
static const char* my_game_version(void) { return "0.2.0"; }

static GameAPI g_api = {
    .game_init      = my_game_init,
    .game_shutdown  = my_game_shutdown,
    .game_frame     = my_game_frame,
    .game_load_map  = my_game_load_map,
    .game_name      = my_game_name,
    .game_version   = my_game_version,
};

GAME_API_EXPORT GameAPI* game_get_api(void) {
    return &g_api;
}
