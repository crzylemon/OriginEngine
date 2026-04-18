/*
 * main.c — Origin Engine entry point
 *
 * Usage: ./origin_engine --game <folder>
 *
 * States: MENU -> PLAYING <-> PAUSED
 * Source-style left-aligned menu with clickable buttons.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "engine/engine.h"
#include "engine/font.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_PAUSED,
} GameState;

static GameState    g_state = STATE_MENU;
static GameAPI*     g_game = NULL;
static GLFWwindow*  g_main_window = NULL;

/* Menu layout — Source style: left side, stacked vertically */
#define MENU_X       0.05f
#define MENU_TITLE_Y 0.15f
#define MENU_TITLE_S 0.035f
#define MENU_START_Y 0.35f
#define MENU_ITEM_S  0.02f
#define MENU_ITEM_H  0.06f

typedef struct {
    const char* label;
    float x, y, scale;
} MenuItem;

static MenuItem g_menu_items[4];
static int      g_menu_count = 0;
static int      g_menu_hover = -1;

/* Show a native error dialog (tries zenity, then xmessage, then just stderr) */
static void show_error_box(const char* title, const char* message) {
    fprintf(stderr, "[engine] ERROR: %s\n%s\n", title, message);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "zenity --error --title=\"%s\" --text=\"%s\" 2>/dev/null", title, message);
    if (system(cmd) != 0) {
        snprintf(cmd, sizeof(cmd),
                 "xmessage -center \"%s: %s\" 2>/dev/null", title, message);
        system(cmd);
    }
}

static void print_usage(const char* exe) {
    printf("Usage: %s --game <folder>\n", exe);
    printf("  Loads <folder>/bin/client.so and <folder>/textures/\n");
}

static int g_death_sound_played = 0;

static void draw_hud(void) {
    Entity* pe = player_get();
    if (!pe) return;

    /* Health display — bottom left */
    char hp[32];
    snprintf(hp, sizeof(hp), "HP: %d", pe->health);
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (pe->health <= 25) { r = 1.0f; g = 0.2f; b = 0.2f; }
    else if (pe->health <= 50) { r = 1.0f; g = 0.7f; b = 0.2f; }
    render_add_text_colored(hp, 0.02f, 0.92f, 0.018f, r, g, b);

    /* Crosshair — center of screen */
    render_add_text_colored("+", 0.494f, 0.48f, 0.015f, 1, 1, 1);

    /* Death screen */
    if (pe->health <= 0) {
        render_set_overlay(1, 0.3f, 0, 0, 0.6f);
        render_add_text_colored("YOU DIED", 0.38f, 0.4f, 0.03f, 1, 0.2f, 0.2f);
        render_add_text_colored("Press R to respawn", 0.35f, 0.5f, 0.015f, 0.8f, 0.8f, 0.8f);
        if (!g_death_sound_played) {
            sound_play("ui/dead.wav");
            g_death_sound_played = 1;
        }
    } else {
        g_death_sound_played = 0;
    }
}

static void tick_world(float dt) {
    int ecount;
    Entity** ents = entity_get_all(&ecount);
    for (int i = 0; i < ecount; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        if (strcmp(e->classname, "player") == 0)
            trigger_check_entity(e);
    }
    for (int i = 0; i < ecount; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        if (e->think && e->next_think > 0) {
            e->next_think -= dt;
            if (e->next_think <= 0) { e->next_think = -1; e->think(e); }
        }
        if (strcmp(e->classname, "player") != 0) {
            if (e->velocity.x != 0 || e->velocity.y != 0 || e->velocity.z != 0)
                e->origin = vec3_add(e->origin, vec3_scale(e->velocity, dt));
        }
    }
    if (g_game->game_frame) g_game->game_frame(dt);
    entity_io_tick(dt);
    prop_physics_tick(dt);
}

static void start_game(void) {
    printf("[engine] starting game...\n");
    if (g_game->game_load_map) g_game->game_load_map("default");
    render_update_brushes();

    /* Feed world brushes to ODE as static colliders */
    {
        int bcount;
        Brush** brushes = brush_get_all(&bcount);
        for (int i = 0; i < bcount; i++) {
            Brush* b = brushes[i];
            if (!b || !b->solid) continue;

            if (b->face_count == 6) {
                /* AABB brush — use box collider (fast) */
                physics_add_static_box(b->mins, b->maxs);
            } else {
                /* Non-AABB brush — build trimesh from face vertices */
                /* Count total triangles (fan triangulation) */
                int total_tris = 0;
                int total_verts = 0;
                for (int f = 0; f < b->face_count; f++) {
                    if (b->faces[f].vertex_count >= 3) {
                        total_tris += b->faces[f].vertex_count - 2;
                        total_verts += b->faces[f].vertex_count;
                    }
                }
                if (total_tris == 0) continue;

                float* verts = (float*)malloc(total_verts * 3 * sizeof(float));
                int* indices = (int*)malloc(total_tris * 3 * sizeof(int));
                int vi = 0, ti = 0, base = 0;

                for (int f = 0; f < b->face_count; f++) {
                    BrushFace* bf = &b->faces[f];
                    if (bf->vertex_count < 3) continue;
                    for (int v = 0; v < bf->vertex_count; v++) {
                        verts[vi++] = bf->vertices[v].x;
                        verts[vi++] = bf->vertices[v].y;
                        verts[vi++] = bf->vertices[v].z;
                    }
                    for (int v = 1; v < bf->vertex_count - 1; v++) {
                        indices[ti++] = base;
                        indices[ti++] = base + v;
                        indices[ti++] = base + v + 1;
                    }
                    base += bf->vertex_count;
                }

                physics_add_static_trimesh(verts, total_verts, indices, total_tris);
                free(verts);
                free(indices);
            }
        }
    }
    /* Feed brush entity brushes to ODE as static colliders */
    {
        int ecount;
        Entity** ents = entity_get_all(&ecount);
        for (int i = 0; i < ecount; i++) {
            Entity* e = ents[i];
            if (!e || (e->flags & EF_DEAD)) continue;
            BrushEntData* bd = brush_entity_data(e);
            if (!bd) continue;
            /* Skip triggers — they're not solid geometry */
            if (strncmp(e->classname, "trigger_", 8) == 0) continue;

            Vec3 offset = vec3_sub(e->origin, bd->spawn_origin);
            for (int j = 0; j < bd->brush_count; j++) {
                Brush* b = bd->brushes[j];
                if (!b || !b->solid) continue;

                /* Build world-space trimesh from brush faces */
                int total_tris = 0, total_verts = 0;
                for (int f = 0; f < b->face_count; f++) {
                    if (b->faces[f].vertex_count >= 3) {
                        total_tris += b->faces[f].vertex_count - 2;
                        total_verts += b->faces[f].vertex_count;
                    }
                }
                if (total_tris == 0) continue;

                float* verts = (float*)malloc(total_verts * 3 * sizeof(float));
                int* indices = (int*)malloc(total_tris * 3 * sizeof(int));
                int vi = 0, ti = 0, base = 0;

                for (int f = 0; f < b->face_count; f++) {
                    BrushFace* bf = &b->faces[f];
                    if (bf->vertex_count < 3) continue;
                    for (int v = 0; v < bf->vertex_count; v++) {
                        verts[vi++] = bf->vertices[v].x + offset.x;
                        verts[vi++] = bf->vertices[v].y + offset.y;
                        verts[vi++] = bf->vertices[v].z + offset.z;
                    }
                    for (int v = 1; v < bf->vertex_count - 1; v++) {
                        indices[ti++] = base;
                        indices[ti++] = base + v;
                        indices[ti++] = base + v + 1;
                    }
                    base += bf->vertex_count;
                }

                physics_add_static_trimesh(verts, total_verts, indices, total_tris);
                free(verts);
                free(indices);
            }
        }
        printf("[physics] brush entity colliders added\n");
    }

    render_set_overlay(0, 0, 0, 0, 0);
    render_clear_text();
    g_state = STATE_PLAYING;
    glfwSetInputMode(g_main_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    printf("[engine] now playing\n");
}

/* Build the menu UI text */
static void build_menu(const char* title, const char** items, int count) {
    render_clear_text();
    render_add_text_colored(title, MENU_X, MENU_TITLE_Y, MENU_TITLE_S, 1,1,1);

    g_menu_count = count;
    g_menu_hover = -1;
    for (int i = 0; i < count; i++) {
        float y = MENU_START_Y + i * MENU_ITEM_H;
        g_menu_items[i].label = items[i];
        g_menu_items[i].x = MENU_X;
        g_menu_items[i].y = y;
        g_menu_items[i].scale = MENU_ITEM_S;
        render_add_text_colored(items[i], MENU_X, y, MENU_ITEM_S, 1,1,1);
    }
}

/* Check which menu item the mouse is hovering, rebuild with highlight */
static int update_menu_hover(const char* title) {
    float mx, my;
    render_get_mouse_screen(&mx, &my);

    int new_hover = -1;
    for (int i = 0; i < g_menu_count; i++) {
        if (font_text_hit_test(g_menu_items[i].label,
                               g_menu_items[i].x, g_menu_items[i].y,
                               g_menu_items[i].scale, mx, my)) {
            new_hover = i;
            break;
        }
    }

    if (new_hover != g_menu_hover) {
        g_menu_hover = new_hover;
        if (new_hover >= 0) sound_play("ui/menuhover.wav");
        render_clear_text();
        render_add_text_colored(title, MENU_X, MENU_TITLE_Y, MENU_TITLE_S, 1,1,1);
        for (int i = 0; i < g_menu_count; i++) {
            float r, g, b;
            if (i == g_menu_hover) {
                r = 0.5f; g = 0.5f; b = 0.5f; /* gray = hovered */
            } else {
                r = 1.0f; g = 1.0f; b = 1.0f; /* white = normal */
            }
            render_add_text_colored(g_menu_items[i].label,
                                     g_menu_items[i].x, g_menu_items[i].y,
                                     g_menu_items[i].scale, r, g, b);
        }
    }

    return new_hover;
}

/* GLFW callbacks for dev console */
static void char_callback(GLFWwindow* w, unsigned int codepoint) {
    (void)w;
    dev_console_char(codepoint);
}

static void console_key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    (void)w; (void)scancode; (void)mods;
    /* Tilde toggles console */
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        dev_console_toggle();
        return;
    }
    if (dev_console_is_open()) {
        dev_console_key(key, action);
    }
}

/* "map" command — load a map by name */
static void cmd_map(const char* args) {
    if (!args || !args[0]) {
        printf("[engine] usage: map <name>\n");
        dev_console_print("usage: map <name>");
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Loading map: %s", args);
    dev_console_print(msg);

    /* Reset world */
    entity_system_shutdown();
    brush_system_shutdown();
    trigger_system_shutdown();
    entity_io_init();
    entity_system_init();
    brush_system_init();
    trigger_system_init();

    /* Load via game */
    if (g_game && g_game->game_load_map)
        g_game->game_load_map(args);

    render_update_brushes();
    dev_console_print("Map loaded.");
}

/* "quit" command */
static void cmd_quit(const char* args) {
    (void)args;
    if (g_main_window)
        glfwSetWindowShouldClose(g_main_window, GLFW_TRUE);
}

/* "noclip" command */
static void cmd_noclip(const char* args) {
    (void)args;
    Entity* pe = player_get();
    PlayerData* pd = player_data(pe);
    if (pd) {
        pd->noclip = !pd->noclip;
        char msg[64];
        snprintf(msg, sizeof(msg), "noclip %s", pd->noclip ? "ON" : "OFF");
        dev_console_print(msg);
    }
}

/* "showtriggers_toggle" command */
static void cmd_showtriggers_toggle(const char* args) {
    (void)args;
    float cur = cvar_get("brush_showtriggers");
    cvar_set("brush_showtriggers", cur < 1.0f ? 1.0f : 0.0f);
    char msg[64];
    snprintf(msg, sizeof(msg), "brush_showtriggers %s", cvar_get("brush_showtriggers") >= 1.0f ? "ON" : "OFF");
    dev_console_print(msg);
}

static void cmd_showinvis_toggle(const char* args) {
    (void)args;
    float cur = cvar_get("brush_showinvis");
    cvar_set("brush_showinvis", cur < 1.0f ? 1.0f : 0.0f);
    char msg[64];
    snprintf(msg, sizeof(msg), "brush_showinvis %s", cvar_get("brush_showinvis") >= 1.0f ? "ON" : "OFF");
    dev_console_print(msg);
}

int main(int argc, char** argv) {
    const char* game_folder = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_folder = argv[++i];
    }
    if (!game_folder) { print_usage(argv[0]); return 1; }

    char so_path[512], tex_path[512];
    snprintf(so_path, sizeof(so_path), "%s/bin/client.so", game_folder);
    snprintf(tex_path, sizeof(tex_path), "%s/textures", game_folder);

    printf("╔══════════════════════════════════════╗\n");
    printf("║          Origin Engine Dev           ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
    printf("[engine] game folder: %s\n", game_folder);

    console_init();
    entity_system_init();
    brush_system_init();
    trigger_system_init();
    entity_io_init();
    dev_console_init();
    sound_init();
    mesh_system_init();
    prop_system_init();
    physics_init();
    texture_load_all(tex_path);

    /* Set sound/mesh base paths */
    {
        char snd_path[512], mesh_path[512];
        snprintf(snd_path, sizeof(snd_path), "%s/sound", game_folder);
        snprintf(mesh_path, sizeof(mesh_path), "%s/meshes", game_folder);
        sound_set_base_path(snd_path);
        mesh_set_base_path(mesh_path);
    }

    if (access(so_path, F_OK) != 0) {
        char msg[600];
        snprintf(msg, sizeof(msg),
                 "Could not find game executable.");
        show_error_box("Error", msg);
        return 1;
    }

    if (!game_dll_load(so_path, game_folder)) {
        char msg[600];
        snprintf(msg, sizeof(msg),
                 "Failed to load game client:\n%s\n\n"
                 "The file exists but could not be loaded.\n"
                 "Check for missing symbols or link errors.", so_path);
        show_error_box("Origin Engine", msg);
        return 1;
    }
    g_game = game_dll_get_api();

    char title[256];
    snprintf(title, sizeof(title), "Origin Engine - %s",
             g_game->game_name ? g_game->game_name() : "Unknown Game");

    if (!render_init(1280, 720, title)) {
        printf("[engine] renderer init failed\n");
        return 1;
    }
    g_main_window = render_get_window();

    /* Set up console callbacks */
    glfwSetCharCallback(g_main_window, char_callback);
    glfwSetKeyCallback(g_main_window, console_key_callback);

    /* Register engine commands */
    cmd_register("map", cmd_map);
    cmd_register("quit", cmd_quit);
    cmd_register("exit", cmd_quit);
    cmd_register("noclip", cmd_noclip);
    cmd_register("showtriggers_toggle", cmd_showtriggers_toggle);
    cmd_register("brush_showtriggers", cmd_showtriggers_toggle);
    cmd_register("brush_showinvis", cmd_showinvis_toggle);
    cvar_register("brush_showtriggers", 0.0f);
    cvar_register("brush_showinvis", 0.0f);
    cvar_register("enable_facelights", 1.0f);
    cvar_register("phy_old", 0.0f);

    /* Main menu */
    const char* game_title = g_game->game_name ? g_game->game_name() : "Origin Engine";
    glfwSetInputMode(g_main_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    render_set_overlay(1, 0, 0, 0, 0.85f);

    const char* main_menu[] = { "New Game", "Continue", "Settings", "Exit" };
    build_menu(game_title, main_menu, 4);

    printf("[engine] === MENU ===\n");

    double last_time = glfwGetTime();
    int prev_click = GLFW_RELEASE;
    int prev_escape = GLFW_RELEASE;

    while (!render_should_close()) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        last_time = now;

        render_poll_events();

        int click = glfwGetMouseButton(g_main_window, GLFW_MOUSE_BUTTON_LEFT);
        int click_pressed = (click == GLFW_PRESS && prev_click == GLFW_RELEASE);
        prev_click = click;

        int escape = glfwGetKey(g_main_window, GLFW_KEY_ESCAPE);
        int escape_pressed = (escape == GLFW_PRESS && prev_escape == GLFW_RELEASE);
        prev_escape = escape;

        /* Console blocks input when open */
        int con_open = dev_console_is_open();

        switch (g_state) {
        case STATE_MENU: {
            if (!con_open) {
                int hover = update_menu_hover(game_title);

                if (click_pressed && hover >= 0) {
                    sound_play("ui/menuclick.wav");
                    switch (hover) {
                    case 0: start_game(); break;
                    case 1: printf("[engine] Continue (not implemented)\n"); break;
                    case 2: printf("[engine] Settings (not implemented)\n"); break;
                    case 3: glfwSetWindowShouldClose(g_main_window, GLFW_TRUE); break;
                    }
                }
                if (escape_pressed)
                    glfwSetWindowShouldClose(g_main_window, GLFW_TRUE);
            }

            if (con_open) { render_clear_text(); dev_console_render(); }
            render_draw_frame((float)now);
            if (con_open) { render_set_overlay(0,0,0,0,0); render_clear_text(); }
            break;
        }

        case STATE_PLAYING: {
            Entity* pe = player_get();
            int dead = (pe && pe->health <= 0);

            if (!con_open && !dead) {
                render_process_input(dt);
                tick_world(dt);
            } else if (!con_open && dead) {
                /* Still tick world for animations, but no player input */
                tick_world(dt);
                /* R to respawn */
                if (glfwGetKey(g_main_window, GLFW_KEY_R) == GLFW_PRESS && pe) {
                    Entity* spawn = entity_find_by_class("info_player_start");
                    pe->origin = spawn ? spawn->origin : VEC3(0, -200, 1);
                    pe->health = 100;
                    pe->velocity = VEC3_ZERO;
                    render_set_overlay(0, 0, 0, 0, 0);
                    sound_play("ui/loadgameORrespawn.wav");
                }
            }

            /* HUD */
            if (!con_open) {
                render_clear_text();
                draw_hud();
            }

            if (con_open) { render_clear_text(); dev_console_render(); }
            render_draw_frame((float)now);
            if (con_open) { render_set_overlay(0,0,0,0,0); render_clear_text(); }

            if (!con_open && !dead && escape_pressed) {
                g_state = STATE_PAUSED;
                glfwSetInputMode(g_main_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                render_set_overlay(1, 0, 0, 0, 0.6f);
                const char* pause_menu[] = { "Resume", "Settings", "Exit to Menu" };
                build_menu("PAUSED", pause_menu, 3);
                g_menu_hover = -1;
            }
            break;
        }

        case STATE_PAUSED: {
            if (!con_open) {
                int hover = update_menu_hover("PAUSED");

                if (click_pressed && hover >= 0) {
                    sound_play("ui/menuclick.wav");
                    switch (hover) {
                    case 0: /* Resume */
                        g_state = STATE_PLAYING;
                        glfwSetInputMode(g_main_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        double mx, my;
                        glfwGetCursorPos(g_main_window, &mx, &my);
                        render_reset_mouse(mx, my);
                        render_set_overlay(0, 0, 0, 0, 0);
                        render_clear_text();
                        break;
                    case 1: printf("[engine] Settings (not implemented)\n"); break;
                    case 2: glfwSetWindowShouldClose(g_main_window, GLFW_TRUE); break;
                    }
                }
                if (escape_pressed) {
                    g_state = STATE_PLAYING;
                    glfwSetInputMode(g_main_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    double mx2, my2;
                    glfwGetCursorPos(g_main_window, &mx2, &my2);
                    render_reset_mouse(mx2, my2);
                    render_set_overlay(0, 0, 0, 0, 0);
                    render_clear_text();
                }
            }

            if (con_open) { render_clear_text(); dev_console_render(); }
            render_draw_frame((float)now);
            if (con_open) { render_set_overlay(0,0,0,0,0); render_clear_text(); }
            break;
        }
        }
    }

    render_shutdown();
    sound_shutdown();
    mesh_system_shutdown();
    prop_system_shutdown();
    physics_shutdown();
    game_dll_unload();
    trigger_system_shutdown();
    brush_system_shutdown();
    entity_system_shutdown();
    texture_free_all();
    console_shutdown();

    printf("Clean exit.\n");
    return 0;
}
