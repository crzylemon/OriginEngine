/*
 * gen_meshes.c — Generate basic .oemesh files
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../engine/mesh.h"
#include "../engine/mesh.c"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void gen_cube(const char* dir) {
    /* Unit cube centered at origin, 32x32x32 */
    float s = 16;
    MeshVertex verts[] = {
        /* Front (+Y) */
        {{-s,-s, s}, {0,0}, {0,-1,0}}, {{ s,-s, s}, {1,0}, {0,-1,0}},
        {{ s,-s,-s}, {1,1}, {0,-1,0}}, {{-s,-s,-s}, {0,1}, {0,-1,0}},
        /* Back (-Y) */
        {{ s, s, s}, {0,0}, {0,1,0}}, {{-s, s, s}, {1,0}, {0,1,0}},
        {{-s, s,-s}, {1,1}, {0,1,0}}, {{ s, s,-s}, {0,1}, {0,1,0}},
        /* Right (+X) */
        {{ s,-s, s}, {0,0}, {1,0,0}}, {{ s, s, s}, {1,0}, {1,0,0}},
        {{ s, s,-s}, {1,1}, {1,0,0}}, {{ s,-s,-s}, {0,1}, {1,0,0}},
        /* Left (-X) */
        {{-s, s, s}, {0,0}, {-1,0,0}}, {{-s,-s, s}, {1,0}, {-1,0,0}},
        {{-s,-s,-s}, {1,1}, {-1,0,0}}, {{-s, s,-s}, {0,1}, {-1,0,0}},
        /* Top (+Z) */
        {{-s, s, s}, {0,0}, {0,0,1}}, {{ s, s, s}, {1,0}, {0,0,1}},
        {{ s,-s, s}, {1,1}, {0,0,1}}, {{-s,-s, s}, {0,1}, {0,0,1}},
        /* Bottom (-Z) */
        {{-s,-s,-s}, {0,0}, {0,0,-1}}, {{ s,-s,-s}, {1,0}, {0,0,-1}},
        {{ s, s,-s}, {1,1}, {0,0,-1}}, {{-s, s,-s}, {0,1}, {0,0,-1}},
    };
    unsigned int indices[] = {
        0,2,1, 0,3,2,     4,6,5, 4,7,6,
        8,10,9, 8,11,10,  12,14,13, 12,15,14,
        16,18,17, 16,19,18, 20,22,21, 20,23,22,
    };
    Mesh* m = mesh_create("crate", "plywood", verts, 24, indices, 36);
    char path[256];
    snprintf(path, sizeof(path), "%s/crate.oemesh", dir);
    mesh_save(m, path);
    mesh_free(m);
}

static void gen_cylinder(const char* dir, const char* name, const char* tex,
                          float radius, float height, int segments) {
    /* Cylinder centered at origin, bottom at z=0, top at z=height */
    int vc = segments * 4 + 2; /* side top/bot rings + top center + bot center */
    int ic = segments * 12;     /* side quads (6) + top fan (3) + bot fan (3) */
    MeshVertex* verts = calloc(vc, sizeof(MeshVertex));
    unsigned int* indices = calloc(ic, sizeof(unsigned int));

    int vi = 0, ii = 0;

    /* Side vertices: two rings */
    for (int i = 0; i < segments; i++) {
        float a = (float)i / segments * 2.0f * M_PI;
        float nx = cosf(a), ny = sinf(a);
        float u = (float)i / segments;
        /* Bottom ring */
        verts[vi] = (MeshVertex){{nx*radius, ny*radius, 0}, {u, 1}, {nx, ny, 0}};
        vi++;
        /* Top ring */
        verts[vi] = (MeshVertex){{nx*radius, ny*radius, height}, {u, 0}, {nx, ny, 0}};
        vi++;
    }
    /* Side indices */
    for (int i = 0; i < segments; i++) {
        int b0 = i * 2, t0 = i * 2 + 1;
        int b1 = ((i+1) % segments) * 2, t1 = ((i+1) % segments) * 2 + 1;
        indices[ii++] = b0; indices[ii++] = b1; indices[ii++] = t1;
        indices[ii++] = b0; indices[ii++] = t1; indices[ii++] = t0;
    }

    /* Top cap */
    int top_center = vi;
    verts[vi++] = (MeshVertex){{0, 0, height}, {0.5f, 0.5f}, {0, 0, 1}};
    for (int i = 0; i < segments; i++) {
        float a = (float)i / segments * 2.0f * M_PI;
        verts[vi] = (MeshVertex){{cosf(a)*radius, sinf(a)*radius, height},
                                  {cosf(a)*0.5f+0.5f, sinf(a)*0.5f+0.5f}, {0,0,1}};
        int next = top_center + 1 + ((i+1) % segments);
        int curr = top_center + 1 + i;
        indices[ii++] = top_center; indices[ii++] = curr; indices[ii++] = next;
        vi++;
    }

    /* Bottom cap */
    int bot_center = vi;
    verts[vi++] = (MeshVertex){{0, 0, 0}, {0.5f, 0.5f}, {0, 0, -1}};
    for (int i = 0; i < segments; i++) {
        float a = (float)i / segments * 2.0f * M_PI;
        verts[vi] = (MeshVertex){{cosf(a)*radius, sinf(a)*radius, 0},
                                  {cosf(a)*0.5f+0.5f, sinf(a)*0.5f+0.5f}, {0,0,-1}};
        int next = bot_center + 1 + ((i+1) % segments);
        int curr = bot_center + 1 + i;
        indices[ii++] = bot_center; indices[ii++] = next; indices[ii++] = curr;
        vi++;
    }

    Mesh* m = mesh_create(name, tex, verts, vi, indices, ii);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.oemesh", dir, name);
    mesh_save(m, path);
    mesh_free(m);
    free(verts);
    free(indices);
}

int main(int argc, char** argv) {
    const char* dir = "game/meshes";
    if (argc > 1) dir = argv[1];

    printf("Origin Engine Mesh Generator\n\n");

    /* Ensure dir exists */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", dir);
    system(cmd);

    gen_cube(dir);
    gen_cylinder(dir, "barrel", "metal", 12, 36, 12);
    gen_cylinder(dir, "pillar_round", "stone", 16, 128, 16);
    gen_cylinder(dir, "lamp_post", "metal", 3, 80, 8);

    printf("\nDone! Generated meshes to %s/\n", dir);
    return 0;
}
