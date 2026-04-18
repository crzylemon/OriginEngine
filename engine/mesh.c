/*
 * mesh.c — Mesh loading, caching, and saving
 */
#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static Mesh*  g_mesh_cache[MAX_MESHES];
static int    g_mesh_count = 0;
static char   g_mesh_base_path[256] = "";

void mesh_system_init(void) {
    memset(g_mesh_cache, 0, sizeof(g_mesh_cache));
    g_mesh_count = 0;
    printf("[mesh] system initialized\n");
}

void mesh_system_shutdown(void) {
    for (int i = 0; i < g_mesh_count; i++) {
        if (g_mesh_cache[i]) mesh_free(g_mesh_cache[i]);
    }
    g_mesh_count = 0;
    printf("[mesh] system shutdown\n");
}

void mesh_set_base_path(const char* path) {
    strncpy(g_mesh_base_path, path, sizeof(g_mesh_base_path) - 1);
}

Mesh* mesh_create(const char* name, const char* texture,
                  const MeshVertex* verts, int vert_count,
                  const unsigned int* indices, int index_count) {
    Mesh* m = (Mesh*)calloc(1, sizeof(Mesh));
    strncpy(m->name, name, 63);
    strncpy(m->texture, texture, 63);
    m->vertex_count = vert_count;
    m->index_count = index_count;
    m->vertices = (MeshVertex*)malloc(vert_count * sizeof(MeshVertex));
    memcpy(m->vertices, verts, vert_count * sizeof(MeshVertex));
    m->indices = (unsigned int*)malloc(index_count * sizeof(unsigned int));
    memcpy(m->indices, indices, index_count * sizeof(unsigned int));
    return m;
}

void mesh_free(Mesh* m) {
    if (!m) return;
    free(m->vertices);
    free(m->indices);
    free(m);
}

Mesh* mesh_load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[mesh] can't open: %s\n", path); return NULL; }

    /* Header */
    char magic[4];
    uint16_t version;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, MESH_MAGIC, 4) != 0) {
        printf("[mesh] invalid magic: %s\n", path);
        fclose(f); return NULL;
    }
    fread(&version, 2, 1, f);

    /* Texture name */
    uint16_t tex_len;
    char texture[64] = {0};
    fread(&tex_len, 2, 1, f);
    if (tex_len > 63) tex_len = 63;
    fread(texture, 1, tex_len, f);

    /* Vertices */
    uint32_t vert_count;
    fread(&vert_count, 4, 1, f);
    MeshVertex* verts = (MeshVertex*)malloc(vert_count * sizeof(MeshVertex));
    fread(verts, sizeof(MeshVertex), vert_count, f);

    /* Indices */
    uint32_t idx_count;
    fread(&idx_count, 4, 1, f);
    unsigned int* indices = (unsigned int*)malloc(idx_count * sizeof(unsigned int));
    fread(indices, sizeof(unsigned int), idx_count, f);

    fclose(f);

    /* Build mesh */
    Mesh* m = (Mesh*)calloc(1, sizeof(Mesh));
    /* Extract name from path */
    const char* slash = strrchr(path, '/');
    const char* fname = slash ? slash + 1 : path;
    strncpy(m->name, fname, 63);
    char* dot = strrchr(m->name, '.');
    if (dot) *dot = '\0';

    strncpy(m->texture, texture, 63);
    m->vertices = verts;
    m->vertex_count = (int)vert_count;
    m->indices = indices;
    m->index_count = (int)idx_count;

    printf("[mesh] loaded '%s': %d verts, %d tris, tex='%s'\n",
           m->name, m->vertex_count, m->index_count / 3, m->texture);
    return m;
}

int mesh_save(const Mesh* m, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("[mesh] can't write: %s\n", path); return 0; }

    fwrite(MESH_MAGIC, 1, 4, f);
    uint16_t version = MESH_VERSION;
    fwrite(&version, 2, 1, f);

    uint16_t tex_len = (uint16_t)strlen(m->texture);
    fwrite(&tex_len, 2, 1, f);
    fwrite(m->texture, 1, tex_len, f);

    uint32_t vc = (uint32_t)m->vertex_count;
    fwrite(&vc, 4, 1, f);
    fwrite(m->vertices, sizeof(MeshVertex), vc, f);

    uint32_t ic = (uint32_t)m->index_count;
    fwrite(&ic, 4, 1, f);
    fwrite(m->indices, sizeof(unsigned int), ic, f);

    fclose(f);
    printf("[mesh] saved '%s': %d verts, %d tris\n", path, m->vertex_count, m->index_count / 3);
    return 1;
}

Mesh* mesh_get(const char* name) {
    /* Check cache */
    for (int i = 0; i < g_mesh_count; i++) {
        if (g_mesh_cache[i] && strcmp(g_mesh_cache[i]->name, name) == 0)
            return g_mesh_cache[i];
    }

    /* Try loading */
    if (g_mesh_count >= MAX_MESHES) return NULL;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s.oemesh", g_mesh_base_path, name);
    Mesh* m = mesh_load(path);
    if (m) {
        g_mesh_cache[g_mesh_count++] = m;
    }
    return m;
}
