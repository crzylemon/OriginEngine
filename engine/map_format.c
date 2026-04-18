/*
 * map_format.c — OEM map format loader/writer
 * Supports: 00=MapData, 01=Brush, 02=Entity, 03=I/O, 04=BrushEntBrush
 */
#include "map_format.h"
#include "brush.h"
#include "brush_entity.h"
#include "entity.h"
#include "entity_io.h"
#include "trigger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Read/Write helpers ──────────────────────────────────────── */

static int read_u8(FILE* f, uint8_t* v) { return fread(v, 1, 1, f) == 1; }
static int read_u16(FILE* f, uint16_t* v) { return fread(v, 2, 1, f) == 1; }
static int read_i16(FILE* f, int16_t* v) { return fread(v, 2, 1, f) == 1; }
static int read_u32(FILE* f, uint32_t* v) { return fread(v, 4, 1, f) == 1; }

static int read_str(FILE* f, char* buf, int maxlen) {
    uint16_t len;
    if (!read_u16(f, &len)) return 0;
    if (len >= maxlen) len = maxlen - 1;
    if (len > 0 && fread(buf, 1, len, f) != len) return 0;
    buf[len] = '\0';
    return 1;
}

static void write_u8(FILE* f, uint8_t v) { fwrite(&v, 1, 1, f); }
static void write_u16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void write_i16(FILE* f, int16_t v) { fwrite(&v, 2, 1, f); }
static void write_u32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }

static void write_str(FILE* f, const char* s) {
    uint16_t len = (uint16_t)strlen(s);
    write_u16(f, len);
    if (len > 0) fwrite(s, 1, len, f);
}

/* ── Shared: read/write a single brush's face data ───────────── */

static int read_brush_data(FILE* f, Brush* b) {
    uint8_t solid;
    uint16_t face_count;
    if (!read_u8(f, &solid)) return 0;
    if (!read_u16(f, &face_count)) return 0;
    b->solid = solid;

    for (uint16_t fi = 0; fi < face_count; fi++) {
        char tex[BRUSH_NAME_LEN];
        if (!read_str(f, tex, BRUSH_NAME_LEN)) return 0;

        uint16_t scale_u, scale_v;
        int16_t off_u, off_v;
        if (!read_u16(f, &scale_u)) return 0;
        if (!read_u16(f, &scale_v)) return 0;
        if (!read_i16(f, &off_u)) return 0;
        if (!read_i16(f, &off_v)) return 0;

        uint8_t vert_count;
        if (!read_u8(f, &vert_count)) return 0;

        Vec3 verts[MAX_FACE_VERTS];
        int vc = vert_count < MAX_FACE_VERTS ? vert_count : MAX_FACE_VERTS;
        for (uint8_t vi = 0; vi < vert_count; vi++) {
            int16_t vx, vy, vz;
            if (!read_i16(f, &vx)) return 0;
            if (!read_i16(f, &vy)) return 0;
            if (!read_i16(f, &vz)) return 0;
            if (vi < vc)
                verts[vi] = VEC3((float)vx, (float)vy, (float)vz);
        }

        int face_idx = brush_add_face(b, verts, vc, tex);
        if (face_idx >= 0) {
            brush_set_face_scale(b, face_idx, (float)scale_u/100.0f, (float)scale_v/100.0f);
            brush_set_face_offset(b, face_idx, (float)off_u/100.0f, (float)off_v/100.0f);
        }
    }
    return 1;
}

static void write_brush_data(FILE* f, Brush* b) {
    write_u8(f, (uint8_t)b->solid);
    write_u16(f, (uint16_t)b->face_count);
    for (int fi = 0; fi < b->face_count; fi++) {
        BrushFace* bf = &b->faces[fi];
        write_str(f, bf->texture);
        write_u16(f, (uint16_t)(bf->scale_u * 100.0f));
        write_u16(f, (uint16_t)(bf->scale_v * 100.0f));
        write_i16(f, (int16_t)(bf->offset_u * 100.0f));
        write_i16(f, (int16_t)(bf->offset_v * 100.0f));
        write_u8(f, (uint8_t)bf->vertex_count);
        for (int vi = 0; vi < bf->vertex_count; vi++) {
            write_i16(f, (int16_t)bf->vertices[vi].x);
            write_i16(f, (int16_t)bf->vertices[vi].y);
            write_i16(f, (int16_t)bf->vertices[vi].z);
        }
    }
}

static uint16_t calc_brush_data_size(Brush* b) {
    uint16_t size = 1 + 2; /* solid + face_count */
    for (int fi = 0; fi < b->face_count; fi++) {
        size += 2 + (uint16_t)strlen(b->faces[fi].texture);
        size += 2 + 2 + 2 + 2 + 1;
        size += (uint16_t)(b->faces[fi].vertex_count * 6);
    }
    return size;
}

/* ── Load ────────────────────────────────────────────────────── */

static int load_map_data(FILE* f, MapInfo* info) {
    if (!read_str(f, info->title, sizeof(info->title))) return 0;
    if (!read_str(f, info->description, sizeof(info->description))) return 0;
    printf("[map] title: '%s'\n", info->title);
    printf("[map] description: '%s'\n", info->description);
    return 1;
}

static int load_brush_seg(FILE* f) {
    Brush* b = brush_create_empty(1);
    if (!b) return 0;
    if (!read_brush_data(f, b)) return 0;
    brush_recompute_bounds(b);
    return 1;
}

/* Stored during load pass 1, resolved in pass 2 */
#define MAX_LOAD_ENTITIES 256
#define MAX_LOAD_IO 256
#define MAX_LOAD_BRUSH_ENTS 64

typedef struct {
    char classname[ENT_NAME_LEN];
    char targetname[ENT_NAME_LEN];
    int16_t ox, oy, oz;
    uint32_t flags;
    uint16_t brush_seg_id; /* 0 = not brush ent, else 1-based segment index */
    char kvs[32][2][128];
    int kv_count;
} LoadEntity;

typedef struct {
    char source[ENT_NAME_LEN];
    char output[32];
    char target[ENT_NAME_LEN];
    char input[32];
    char parameter[ENT_NAME_LEN];
    uint16_t delay_100;
    uint8_t once;
} LoadIO;

typedef struct {
    uint16_t seg_index; /* which segment index this is */
    Brush* brushes[16];
    int brush_count;
} LoadBrushEnt;

static LoadEntity g_load_ents[MAX_LOAD_ENTITIES];
static int g_load_ent_count;
static LoadIO g_load_ios[MAX_LOAD_IO];
static int g_load_io_count;
static LoadBrushEnt g_load_brush_ents[MAX_LOAD_BRUSH_ENTS];
static int g_load_brush_ent_count;

static int load_entity_seg(FILE* f) {
    if (g_load_ent_count >= MAX_LOAD_ENTITIES) return 0;
    LoadEntity* le = &g_load_ents[g_load_ent_count++];
    memset(le, 0, sizeof(LoadEntity));

    if (!read_str(f, le->classname, ENT_NAME_LEN)) return 0;
    if (!read_str(f, le->targetname, ENT_NAME_LEN)) return 0;
    if (!read_i16(f, &le->ox)) return 0;
    if (!read_i16(f, &le->oy)) return 0;
    if (!read_i16(f, &le->oz)) return 0;
    if (!read_u32(f, &le->flags)) return 0;
    if (!read_u16(f, &le->brush_seg_id)) return 0;

    uint16_t kv_count;
    if (!read_u16(f, &kv_count)) return 0;
    le->kv_count = kv_count > 32 ? 32 : kv_count;
    for (int i = 0; i < le->kv_count; i++) {
        if (!read_str(f, le->kvs[i][0], 128)) return 0;
        if (!read_str(f, le->kvs[i][1], 128)) return 0;
    }

    printf("[map] entity: '%s' name='%s' at (%d,%d,%d)%s\n",
           le->classname, le->targetname, le->ox, le->oy, le->oz,
           le->brush_seg_id ? " [brush ent]" : "");
    return 1;
}

static int load_io_seg(FILE* f) {
    if (g_load_io_count >= MAX_LOAD_IO) return 0;
    LoadIO* io = &g_load_ios[g_load_io_count++];
    memset(io, 0, sizeof(LoadIO));

    if (!read_str(f, io->source, ENT_NAME_LEN)) return 0;
    if (!read_str(f, io->output, 32)) return 0;
    if (!read_str(f, io->target, ENT_NAME_LEN)) return 0;
    if (!read_str(f, io->input, 32)) return 0;
    if (!read_str(f, io->parameter, ENT_NAME_LEN)) return 0;
    if (!read_u16(f, &io->delay_100)) return 0;
    if (!read_u8(f, &io->once)) return 0;

    printf("[map] io: %s.%s -> %s.%s (delay=%.2f%s)\n",
           io->source, io->output, io->target, io->input,
           io->delay_100 / 100.0f, io->once ? " once" : "");
    return 1;
}

static int load_brush_ent_seg(FILE* f, uint16_t seg_index) {
    if (g_load_brush_ent_count >= MAX_LOAD_BRUSH_ENTS) return 0;
    LoadBrushEnt* lbe = &g_load_brush_ents[g_load_brush_ent_count++];
    lbe->seg_index = seg_index;
    lbe->brush_count = 0;

    uint16_t brush_count;
    if (!read_u16(f, &brush_count)) return 0;

    for (uint16_t i = 0; i < brush_count && lbe->brush_count < 16; i++) {
        Brush* b = brush_create_empty(1);
        if (!b) return 0;
        if (!read_brush_data(f, b)) return 0;
        b->entity_owned = 1;
        brush_recompute_bounds(b);
        lbe->brushes[lbe->brush_count++] = b;
    }
    return 1;
}

/* Resolve loaded data into actual engine objects */
static void resolve_loaded_data(void) {
    /* Create entities, linking brush ents to their brushes */
    for (int i = 0; i < g_load_ent_count; i++) {
        LoadEntity* le = &g_load_ents[i];
        Entity* ent;

        if (le->brush_seg_id > 0) {
            /* Brush entity */
            ent = brush_entity_create(le->classname, le->targetname);
            BrushEntData* bd = brush_entity_data(ent);
            if (bd) {
                bd->spawn_origin = VEC3_ZERO;
                /* Find matching brush ent data */
                for (int j = 0; j < g_load_brush_ent_count; j++) {
                    if (g_load_brush_ents[j].seg_index == le->brush_seg_id) {
                        for (int k = 0; k < g_load_brush_ents[j].brush_count; k++) {
                            bd->brushes[bd->brush_count++] = g_load_brush_ents[j].brushes[k];
                        }
                        break;
                    }
                }
            }
        } else {
            ent = entity_spawn();
            strncpy(ent->classname, le->classname, ENT_NAME_LEN - 1);
            strncpy(ent->targetname, le->targetname, ENT_NAME_LEN - 1);
        }

        ent->origin = VEC3((float)le->ox, (float)le->oy, (float)le->oz);
        ent->flags = (int)le->flags;

        for (int k = 0; k < le->kv_count; k++) {
            if (strcmp(le->kvs[k][0], "health") == 0)
                ent->health = atoi(le->kvs[k][1]);
            if (strcmp(le->kvs[k][0], "target") == 0)
                strncpy(ent->target, le->kvs[k][1], ENT_NAME_LEN - 1);
        }
    }

    /* Create I/O connections */
    for (int i = 0; i < g_load_io_count; i++) {
        LoadIO* io = &g_load_ios[i];
        Entity* source = entity_find_by_name(io->source);
        if (source) {
            entity_io_connect(source, io->output, io->target, io->input,
                              io->parameter, io->delay_100 / 100.0f, io->once);
        } else {
            printf("[map] WARNING: I/O source '%s' not found\n", io->source);
        }
    }
}

int map_load(const char* path, MapInfo* out_info) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        printf("[map] can't open: %s\n", path);
        return 0;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, OEM_MAGIC, 4) != 0) {
        printf("[map] invalid magic in %s\n", path);
        fclose(f); return 0;
    }

    uint32_t seg_count;
    if (!read_u32(f, &seg_count)) { fclose(f); return 0; }
    printf("[map] loading %s (%u segments)\n", path, seg_count);

    memset(out_info, 0, sizeof(MapInfo));
    g_load_ent_count = 0;
    g_load_io_count = 0;
    g_load_brush_ent_count = 0;

    for (uint32_t i = 0; i < seg_count; i++) {
        uint16_t seg_size;
        uint8_t seg_type;
        if (!read_u16(f, &seg_size)) break;
        if (!read_u8(f, &seg_type)) break;
        long seg_start = ftell(f);

        switch (seg_type) {
            case SEG_MAP_DATA:
                load_map_data(f, out_info);
                break;
            case SEG_BRUSH:
                load_brush_seg(f);
                break;
            case SEG_ENTITY:
                load_entity_seg(f);
                break;
            case SEG_IO_CONNECTION:
                load_io_seg(f);
                break;
            case SEG_BRUSH_ENT:
                load_brush_ent_seg(f, (uint16_t)(i + 1)); /* 1-based */
                break;
            default:
                printf("[map] unknown segment %02x, skipping\n", seg_type);
                break;
        }

        fseek(f, seg_start + seg_size, SEEK_SET);
    }

    fclose(f);

    /* Resolve entities and I/O */
    resolve_loaded_data();

    printf("[map] loaded: %d brushes, %d entities\n", brush_count(), entity_count());
    return 1;
}

/* ── Save ────────────────────────────────────────────────────── */

int map_save(const char* path, const MapInfo* info) {
    FILE* f = fopen(path, "wb");
    if (!f) { printf("[map] can't write: %s\n", path); return 0; }

    int bcount; Brush** brushes = brush_get_all(&bcount);
    int ecount; Entity** ents = entity_get_all(&ecount);

    /*
     * Plan the segment layout:
     * 1. Map data (1 segment)
     * 2. World brushes (non-entity-owned)
     * 3. Brush entity brush groups (04 segments)
     * 4. Entities (02 segments) — with brush_seg_id pointing to their 04
     * 5. I/O connections (03 segments)
     */

    /* Count segments */
    uint32_t seg_count = 1; /* map data */

    /* World brushes */
    int world_brush_count = 0;
    for (int i = 0; i < bcount; i++)
        if (brushes[i] && !brushes[i]->entity_owned) world_brush_count++;
    seg_count += world_brush_count;

    /* Brush entity groups: one 04 segment per brush entity */
    /* Track which entities are brush ents and their 04 segment index */
    int brush_ent_seg_ids[MAX_LOAD_ENTITIES];
    int brush_ent_count = 0;
    memset(brush_ent_seg_ids, 0, sizeof(brush_ent_seg_ids));

    for (int i = 0; i < ecount; i++) {
        if (!ents[i] || (ents[i]->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(ents[i]);
        if (bd && bd->brush_count > 0) {
            seg_count++; /* one 04 segment */
            brush_ent_seg_ids[i] = seg_count; /* 1-based index of this 04 seg */
            brush_ent_count++;
        }
    }

    /* Entities */
    int entity_seg_count = 0;
    for (int i = 0; i < ecount; i++)
        if (ents[i] && !(ents[i]->flags & EF_DEAD)) entity_seg_count++;
    seg_count += entity_seg_count;

    /* I/O connections — scan all entities for connections */
    int io_seg_count = 0;
    for (int i = 0; i < ecount; i++) {
        if (!ents[i] || (ents[i]->flags & EF_DEAD)) continue;
        EntityIO* eio = entity_io_get(ents[i]);
        if (eio) io_seg_count += eio->count;
    }
    seg_count += io_seg_count;

    /* ── Write header ──────────────────────────────────────── */
    fwrite(OEM_MAGIC, 1, 4, f);
    write_u32(f, seg_count);

    /* ── 00: Map data ──────────────────────────────────────── */
    {
        uint16_t size = 2 + (uint16_t)strlen(info->title) + 2 + (uint16_t)strlen(info->description);
        write_u16(f, size);
        write_u8(f, SEG_MAP_DATA);
        write_str(f, info->title);
        write_str(f, info->description);
    }

    /* ── 01: World brushes ─────────────────────────────────── */
    for (int i = 0; i < bcount; i++) {
        if (!brushes[i] || brushes[i]->entity_owned) continue;
        uint16_t size = calc_brush_data_size(brushes[i]);
        write_u16(f, size);
        write_u8(f, SEG_BRUSH);
        write_brush_data(f, brushes[i]);
    }

    /* ── 04: Brush entity brush groups ─────────────────────── */
    for (int i = 0; i < ecount; i++) {
        if (!ents[i] || (ents[i]->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(ents[i]);
        if (!bd || bd->brush_count == 0) continue;

        /* Calculate size: 2 (brush count) + sum of brush data sizes */
        uint16_t size = 2;
        for (int j = 0; j < bd->brush_count; j++)
            size += calc_brush_data_size(bd->brushes[j]);

        write_u16(f, size);
        write_u8(f, SEG_BRUSH_ENT);
        write_u16(f, (uint16_t)bd->brush_count);
        for (int j = 0; j < bd->brush_count; j++)
            write_brush_data(f, bd->brushes[j]);
    }

    /* ── 02: Entities ──────────────────────────────────────── */
    for (int i = 0; i < ecount; i++) {
        if (!ents[i] || (ents[i]->flags & EF_DEAD)) continue;
        Entity* e = ents[i];

        char* targetname = e->targetname;
        uint16_t brush_seg = (uint16_t)brush_ent_seg_ids[i];

        /* Count KVs */
        int kv_count = 0;
        if (e->health != 100) kv_count++;
        if (e->target[0]) kv_count++;

        uint16_t size = 2 + (uint16_t)strlen(e->classname)
                       + 2 + (uint16_t)strlen(targetname)
                       + 6 + 4 + 2 + 2; /* origin + flags + brush_seg + kv_count */
        if (e->health != 100) size += 2 + 6 + 2 + 12;
        if (e->target[0]) size += 2 + 6 + 2 + (uint16_t)strlen(e->target);

        write_u16(f, size);
        write_u8(f, SEG_ENTITY);
        write_str(f, e->classname);
        write_str(f, targetname);
        write_i16(f, (int16_t)e->origin.x);
        write_i16(f, (int16_t)e->origin.y);
        write_i16(f, (int16_t)e->origin.z);
        write_u32(f, (uint32_t)e->flags);
        write_u16(f, brush_seg);
        write_u16(f, (uint16_t)kv_count);
        if (e->health != 100) {
            write_str(f, "health");
            char val[16]; snprintf(val, sizeof(val), "%d", e->health);
            write_str(f, val);
        }
        if (e->target[0]) {
            write_str(f, "target");
            write_str(f, e->target);
        }
    }

    /* ── 03: I/O connections ───────────────────────────────── */
    for (int i = 0; i < ecount; i++) {
        if (!ents[i] || (ents[i]->flags & EF_DEAD)) continue;
        EntityIO* eio = entity_io_get(ents[i]);
        if (!eio) continue;

        for (int j = 0; j < eio->count; j++) {
            IOConnection* c = &eio->connections[j];
            uint16_t size = 2 + (uint16_t)strlen(ents[i]->targetname)
                          + 2 + (uint16_t)strlen(c->output)
                          + 2 + (uint16_t)strlen(c->target)
                          + 2 + (uint16_t)strlen(c->input)
                          + 2 + (uint16_t)strlen(c->parameter)
                          + 2 + 1; /* delay + once */

            write_u16(f, size);
            write_u8(f, SEG_IO_CONNECTION);
            write_str(f, ents[i]->targetname);
            write_str(f, c->output);
            write_str(f, c->target);
            write_str(f, c->input);
            write_str(f, c->parameter);
            write_u16(f, (uint16_t)(c->delay * 100.0f));
            write_u8(f, (uint8_t)c->once);
        }
    }

    fclose(f);
    printf("[map] saved: %s (%u segments)\n", path, seg_count);
    return 1;
}
