/*
 * mesh.h — Origin Engine Mesh format (.oemesh)
 *
 * Simple binary mesh: vertices with position + UV + normal,
 * triangle indices, and a texture name.
 *
 * Format:
 *   Header: "OEMs" (4 bytes) + version u16
 *   Texture name: length u16 + ASCII
 *   Vertex count: u32
 *   Vertices: [pos.x f32, pos.y f32, pos.z f32, u f32, v f32, nx f32, ny f32, nz f32] * count
 *   Triangle count: u32
 *   Indices: [u32, u32, u32] * count
 *
 * Meshes are centered at origin. Entities position them in the world.
 */
#ifndef MESH_H
#define MESH_H

#include "vec3.h"

#define MESH_MAGIC "OEMs"
#define MESH_VERSION 1
#define MAX_MESHES 64

typedef struct {
    float pos[3];
    float uv[2];
    float normal[3];
} MeshVertex;

typedef struct {
    char        name[64];       /* mesh name (filename without ext) */
    char        texture[64];    /* texture name */
    MeshVertex* vertices;
    int         vertex_count;
    unsigned int* indices;
    int         index_count;    /* number of indices (triangles * 3) */
} Mesh;

/* Load a mesh from file. Returns NULL on failure. */
Mesh* mesh_load(const char* path);

/* Free a mesh */
void  mesh_free(Mesh* m);

/* Get a cached mesh by name (loads from mesh_path/name.oemesh if not cached) */
Mesh* mesh_get(const char* name);

/* Set the base path for mesh files */
void  mesh_set_base_path(const char* path);

/* Save a mesh to file */
int   mesh_save(const Mesh* m, const char* path);

/* Create a mesh programmatically */
Mesh* mesh_create(const char* name, const char* texture,
                  const MeshVertex* verts, int vert_count,
                  const unsigned int* indices, int index_count);

void  mesh_system_init(void);
void  mesh_system_shutdown(void);

#endif /* MESH_H */
