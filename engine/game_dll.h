/*
 * game_dll.h — Dynamic game library loader
 *
 * Loads a game .so at runtime and wires up the API.
 */
#ifndef GAME_DLL_H
#define GAME_DLL_H

#include "game_interface.h"

/* Load a game .so and get its API. Returns 1 on success. */
int         game_dll_load(const char* path);

/* Get the loaded game's API */
GameAPI*    game_dll_get_api(void);

/* Get the engine API (populated with real function pointers) */
EngineAPI*  game_dll_get_engine_api(void);

/* Unload the game .so */
void        game_dll_unload(void);

#endif /* GAME_DLL_H */
