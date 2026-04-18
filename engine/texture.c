/*
 * texture.c — Texture loading implementation
 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

static int g_stbi_flipped = 0;

static TextureInfo  g_textures[MAX_TEXTURES];
static unsigned char* g_pixels[MAX_TEXTURES];
static int          g_tex_count = 0;

/* Tool texture names that get special treatment */
static const char* tool_textures[] = {
    "nodraw", "trigger", "entclip", "plrclip", NULL
};

static int is_tool_texture(const char* name) {
    for (int i = 0; tool_textures[i]; i++) {
        if (strcmp(name, tool_textures[i]) == 0) return 1;
    }
    return 0;
}

static int has_transparency(unsigned char* pixels, int w, int h) {
    for (int i = 0; i < w * h; i++) {
        if (pixels[i * 4 + 3] < 255) return 1;
    }
    return 0;
}

/* Strip extension from filename */
static void strip_ext(const char* filename, char* out, int maxlen) {
    strncpy(out, filename, maxlen - 1);
    out[maxlen - 1] = '\0';
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

int texture_load_all(const char* directory) {
    DIR* dir = opendir(directory);
    if (!dir) {
        printf("[texture] can't open directory: %s\n", directory);
        return 0;
    }

    struct dirent* entry;
    if (!g_stbi_flipped) {
        stbi_set_flip_vertically_on_load(1);
        g_stbi_flipped = 1;
    }
    while ((entry = readdir(dir)) != NULL && g_tex_count < MAX_TEXTURES) {
        const char* name = entry->d_name;
        /* Check for image extensions */
        const char* ext = strrchr(name, '.');
        if (!ext) continue;
        if (strcmp(ext, ".png") != 0 && strcmp(ext, ".jpg") != 0 &&
            strcmp(ext, ".jpeg") != 0) continue;

        /* Build full path */
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", directory, name);

        /* Load with stb_image (force RGBA) */
        int w, h, channels;
        unsigned char* pixels = stbi_load(path, &w, &h, &channels, 4);
        if (!pixels) {
            printf("[texture] failed to load: %s\n", path);
            continue;
        }

        TextureInfo* tex = &g_textures[g_tex_count];
        strip_ext(name, tex->name, TEX_NAME_LEN);
        tex->width = w;
        tex->height = h;
        tex->transparent = has_transparency(pixels, w, h);
        tex->is_tool = is_tool_texture(tex->name);
        tex->nodraw = (strcmp(tex->name, "nodraw") == 0);

        g_pixels[g_tex_count] = pixels;

        printf("[texture] loaded '%s' (%dx%d%s%s)\n",
               tex->name, w, h,
               tex->transparent ? " transparent" : "",
               tex->is_tool ? " tool" : "");

        g_tex_count++;
    }

    closedir(dir);
    printf("[texture] %d textures loaded\n", g_tex_count);
    return g_tex_count;
}

int texture_find(const char* name) {
    for (int i = 0; i < g_tex_count; i++) {
        if (strcmp(g_textures[i].name, name) == 0) return i;
    }
    return -1;
}

const TextureInfo* texture_get_info(int index) {
    if (index < 0 || index >= g_tex_count) return NULL;
    return &g_textures[index];
}

int texture_count(void) {
    return g_tex_count;
}

unsigned char* texture_get_pixels(int index) {
    if (index < 0 || index >= g_tex_count) return NULL;
    return g_pixels[index];
}

void texture_free_all(void) {
    for (int i = 0; i < g_tex_count; i++) {
        if (g_pixels[i]) stbi_image_free(g_pixels[i]);
        g_pixels[i] = NULL;
    }
    g_tex_count = 0;
    printf("[texture] freed all textures\n");
}
