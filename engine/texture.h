/*
 * texture.h — Texture loading and management for Origin Engine
 *
 * Loads PNG/JPG textures, uploads to a Vulkan texture array.
 * Brushes reference textures by name.
 */
#ifndef TEXTURE_H
#define TEXTURE_H

#define MAX_TEXTURES 64
#define TEX_NAME_LEN 64

typedef struct {
    char    name[TEX_NAME_LEN];  /* e.g. "bricks" */
    int     width, height;
    int     transparent;          /* has alpha < 255 */
    int     is_tool;              /* nodraw, trigger, clip, etc */
    int     nodraw;               /* don't render at all */
} TextureInfo;

/* Load all textures from a directory. Call before render_init pipeline setup. */
int  texture_load_all(const char* directory);

/* Find texture index by name (without extension). Returns -1 if not found. */
int  texture_find(const char* name);

/* Get info about a texture */
const TextureInfo* texture_get_info(int index);

/* How many textures loaded */
int  texture_count(void);

/* Get raw pixel data for a texture (RGBA, 4 bytes per pixel) */
unsigned char* texture_get_pixels(int index);

void texture_free_all(void);

#endif /* TEXTURE_H */
