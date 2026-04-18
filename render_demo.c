/*
 * render_demo.c — Origin Engine with dynamic game .so loading
 *
 * The engine loads game/game.so which builds the map and spawns entities.
 */
#include <stdio.h>
#include "engine/engine.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

int main(int argc, char** argv) {
    const char* game_path = "game/game.so";
    if (argc > 1) game_path = argv[1];

    printf("╔══════════════════════════════════════╗\n");
    printf("║        Origin Engine v0.1            ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* Init engine systems */
    console_init();
    entity_system_init();
    brush_system_init();
    trigger_system_init();

    /* Load textures */
    texture_load_all("textures");

    /* Load game .so */
    if (!game_dll_load(game_path)) {
        printf("Failed to load game: %s\n", game_path);
        return 1;
    }

    /* Tell the game to load a map */
    GameAPI* game = game_dll_get_api();
    if (game->game_load_map) {
        game->game_load_map("demo_map");
    }

    /* Init renderer */
    if (!render_init(1280, 720, "Origin Engine")) {
        printf("Failed to init renderer!\n");
        return 1;
    }

    /* Upload brush geometry */
    render_update_brushes();

    /* Main loop */
    double last_time = glfwGetTime();
    while (!render_should_close()) {
        double now = glfwGetTime();
        float dt = (float)(now - last_time);
        last_time = now;

        render_poll_events();
        render_process_input(dt);

        /* Call game frame */
        if (game->game_frame) game->game_frame(dt);

        render_draw_frame((float)now);
    }

    /* Shutdown */
    render_shutdown();
    game_dll_unload();
    trigger_system_shutdown();
    brush_system_shutdown();
    entity_system_shutdown();
    texture_free_all();
    console_shutdown();

    printf("Clean exit.\n");
    return 0;
}
