/*
 * map_compiler.c — Builds the demo map and saves it as .oem
 *
 * Usage: ./map_compiler <output.oem>
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../engine/entity.h"
#include "../engine/entity.c"
#include "../engine/brush.h"
#include "../engine/brush.c"
#include "../engine/brush_entity.h"
#include "../engine/brush_entity.c"
#include "../engine/entity_io.h"
#include "../engine/entity_io.c"
#include "../engine/trigger.h"
#include "../engine/trigger.c"
#include "../engine/map_format.h"
#include "../engine/map_format.c"

/* Helper: set face scale+offset to fit texture exactly once on a brush face.
   mins/maxs are the brush world coords. */
static void fit_texture_to_face(Brush* b, int face, float x0, float y0, float z0,
                                 float xsize, float ysize, float zsize) {
    float T = 1.0f / 128.0f;
    /* Scale = size * T so that size maps to UV range 0..1 */
    switch (face) {
    case FACE_SOUTH: case FACE_NORTH: /* Y-dominant: u=X, v=Z */
        brush_set_face_scale(b, face, xsize * T, zsize * T);
        brush_set_face_offset(b, face, -x0 * T / (xsize * T), -z0 * T / (zsize * T));
        break;
    case FACE_EAST: case FACE_WEST: /* X-dominant: u=Y, v=Z */
        brush_set_face_scale(b, face, ysize * T, zsize * T);
        brush_set_face_offset(b, face, -y0 * T / (ysize * T), -z0 * T / (zsize * T));
        break;
    case FACE_TOP: case FACE_BOTTOM: /* Z-dominant: u=X, v=Y */
        brush_set_face_scale(b, face, xsize * T, ysize * T);
        brush_set_face_offset(b, face, -x0 * T / (xsize * T), -y0 * T / (ysize * T));
        break;
    }
}


int main(int argc, char** argv) {
    const char* output = "game/maps/default.oem";
    if (argc > 1) output = argv[1];
    printf("Origin Engine Map Compiler\n\n");
    entity_system_init();
    brush_system_init();
    entity_io_init();
    trigger_system_init();
    Brush* b;

    /* Floor (split for pit) */
    b = brush_create(VEC3(-512,-290,-16), VEC3(512,512,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);
    b = brush_create(VEC3(-512,-512,-16), VEC3(-150,-290,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);
    b = brush_create(VEC3(150,-512,-16), VEC3(512,-290,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);
    b = brush_create(VEC3(-150,-512,-16), VEC3(150,-450,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);

    /* Outer walls (east split for doorway, west split for outdoor) */
    /* West wall: split sections created in outdoor area below */
    b = brush_create(VEC3(-512,-512,0), VEC3(512,-496,160), "bricks", 1);
    brush_set_face_texture(b, FACE_SOUTH, "nodraw");
    b = brush_create(VEC3(-512,496,0), VEC3(512,512,160), "bricks", 1);
    brush_set_face_texture(b, FACE_NORTH, "nodraw");
    /* East wall with doorway */
    b = brush_create(VEC3(496,-512,0), VEC3(512,-64,160), "bricks", 1);
    brush_set_face_texture(b, FACE_EAST, "wall_office");
    b = brush_create(VEC3(496,64,0), VEC3(512,512,160), "bricks", 1);
    brush_set_face_texture(b, FACE_EAST, "wall_office");
    b = brush_create(VEC3(496,-64,100), VEC3(512,64,160), "bricks", 1);
    brush_set_face_texture(b, FACE_EAST, "wall_office");

    /* Pillars */
    brush_create(VEC3(-120,-120,0), VEC3(-100,-100,140), "stone", 1);
    brush_create(VEC3(100,-120,0), VEC3(120,-100,140), "stone", 1);
    brush_create(VEC3(-120,100,0), VEC3(-100,120,140), "stone", 1);
    brush_create(VEC3(100,100,0), VEC3(120,120,140), "stone", 1);
    b = brush_create(VEC3(-140,-140,140), VEC3(140,140,150), "concrete", 1);
    brush_set_face_texture(b, FACE_BOTTOM, "plywood");

    /* Office */
    b = brush_create(VEC3(512,-200,-16), VEC3(800,200,0), "concrete", 1);
    brush_set_face_texture(b, FACE_TOP, "carpet_blue");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);
    brush_create(VEC3(800,-200,0), VEC3(816,200,120), "wall_office", 1);
    brush_create(VEC3(512,-200,0), VEC3(816,-184,120), "wall_office", 1);
    brush_create(VEC3(512,184,0), VEC3(816,200,120), "wall_office", 1);
    b = brush_create(VEC3(512,-200,120), VEC3(816,200,130), "concrete", 1);
    brush_set_face_texture(b, FACE_BOTTOM, "tile");
    /* Desk + computer */
    brush_create(VEC3(685,-35,0), VEC3(755,35,31), "wood", 1);
    b = brush_create(VEC3(685,-35,31), VEC3(755,35,36), "wood", 1);
    brush_set_face_texture(b, FACE_TOP, "carpet_red");
    b = brush_create(VEC3(700,-20,36), VEC3(740,20,76), "computerSide", 1);
    brush_set_face_texture(b, FACE_TOP, "metal");
    brush_set_face_texture(b, FACE_BOTTOM, "metal");
    brush_set_face_texture(b, FACE_SOUTH, "computerFront");
    brush_set_face_texture(b, FACE_NORTH, "computerBack");

    /* Pit */
    b = brush_create(VEC3(-150,-450,-100), VEC3(150,-300,-90), "stone_dirty", 1);
    brush_create(VEC3(-150,-450,-90), VEC3(-140,-300,0), "rock", 1);
    brush_create(VEC3(140,-450,-90), VEC3(150,-300,0), "rock", 1);
    brush_create(VEC3(-150,-450,-90), VEC3(150,-440,0), "rock", 1);
    brush_create(VEC3(-150,-300,-90), VEC3(150,-290,0), "rock", 1);
    brush_create(VEC3(-100,-420,-89), VEC3(100,-320,-88), "lava", 0);

    /* Trap door */
    Entity* trap = brush_entity_create("func_door", "trap_door");
    trap->origin = VEC3(0, -375, 0);
    trap->flags = EF_SOLID;
    brush_entity_data(trap)->spawn_origin = trap->origin;
    b = brush_entity_add_brush(trap, VEC3(-150,-75,-6), VEC3(150,75,-1), "concrete", 1);
    brush_set_face_texture(b, FACE_TOP, "sand");
    Entity* trap_trig = brush_entity_create("trigger_once", "trap_trigger");
    trap_trig->origin = VEC3(0, -375, 0);
    trap_trig->flags = 0;
    brush_entity_data(trap_trig)->spawn_origin = trap_trig->origin;
    brush_entity_add_brush(trap_trig, VEC3(-100,-45,-2), VEC3(100,45,40), "trigger", 0);
    entity_io_connect(trap_trig, "OnTrigger", "trap_door", "SlideOpen", "", 0.5f, 0);

    /* Lava hurt */
    Entity* lava_hurt = brush_entity_create("trigger_hurt", "lava_damage");
    lava_hurt->origin = VEC3(0, -370, -90);
    lava_hurt->flags = 0;
    brush_entity_data(lava_hurt)->spawn_origin = lava_hurt->origin;
    brush_entity_add_brush(lava_hurt, VEC3(-100,-50,-5), VEC3(100,50,20), "trigger", 0);

    /* Platform + ramp */
    b = brush_create(VEC3(-80,250,80), VEC3(80,400,90), "metal", 1);
    {
        Brush* wedge = brush_create_empty(1);
        float wx0=-80, wx1=80, wy0=150, wy1=250, wz0=0, wz1=90;
        Vec3 bottom[] = {{wx0,wy1,wz0},{wx1,wy1,wz0},{wx1,wy0,wz0},{wx0,wy0,wz0}};
        brush_add_face(wedge, bottom, 4, "concrete");
        Vec3 back[] = {{wx0,wy1,wz0},{wx0,wy1,wz1},{wx1,wy1,wz1},{wx1,wy1,wz0}};
        brush_add_face(wedge, back, 4, "concrete");
        Vec3 slope[] = {{wx1,wy0,wz0},{wx1,wy1,wz1},{wx0,wy1,wz1},{wx0,wy0,wz0}};
        brush_add_face(wedge, slope, 4, "metal");
        Vec3 left[] = {{wx0,wy1,wz0},{wx0,wy0,wz0},{wx0,wy1,wz1}};
        brush_add_face(wedge, left, 3, "concrete");
        Vec3 right[] = {{wx1,wy0,wz0},{wx1,wy1,wz0},{wx1,wy1,wz1}};
        brush_add_face(wedge, right, 3, "concrete");
        brush_recompute_bounds(wedge);
    }
    brush_create(VEC3(-80,250,90), VEC3(-76,400,120), "metal", 1);
    brush_create(VEC3(76,250,90), VEC3(80,400,120), "metal", 1);
    brush_create(VEC3(-80,396,90), VEC3(80,400,120), "metal", 1);

    /* Hex pillar */
    {
        Brush* hex = brush_create_empty(1);
        float cx=-300, cy=0, r=30, h=120;
        Vec3 tv[6], bv[6];
        for (int i=0;i<6;i++) {
            float a=(float)i*3.14159f*2.0f/6.0f;
            bv[i]=VEC3(cx+r*cosf(a),cy+r*sinf(a),0);
            tv[i]=VEC3(cx+r*cosf(a),cy+r*sinf(a),h);
        }
        brush_add_face(hex, tv, 6, "stone");
        Vec3 br[6]; for(int i=0;i<6;i++) br[i]=bv[5-i];
        brush_add_face(hex, br, 6, "stone_dirty");
        for(int i=0;i<6;i++){int j=(i+1)%6;
            Vec3 s[]={bv[i],bv[j],tv[j],tv[i]};
            brush_add_face(hex,s,4,"stone");}
        brush_recompute_bounds(hex);
    }

    /* ════════════════════════════════════════════════════════════
     *  Outdoor area: grass beyond west wall, drops to skybox
     * ════════════════════════════════════════════════════════════ */

    /* Remove part of west wall for outdoor opening (y=-100..100) */
    /* The west wall was created as full, so we need gap sections */
    /* West wall south section */
    b = brush_create(VEC3(-512,-512,0), VEC3(-496,-100,160), "bricks", 1);
    brush_set_face_texture(b, FACE_WEST, "nodraw");
    /* West wall north section */
    b = brush_create(VEC3(-512,100,0), VEC3(-496,512,160), "bricks", 1);
    brush_set_face_texture(b, FACE_WEST, "nodraw");
    /* Lintel above opening */
    brush_create(VEC3(-512,-100,120), VEC3(-496,100,160), "bricks", 1);

    /* Outdoor grass patch */
    b = brush_create(VEC3(-700,-150,-16), VEC3(-512,150,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass_clean");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);

    /* Small grass extension that drops off */
    b = brush_create(VEC3(-800,-100,-16), VEC3(-700,100,0), "dirt", 1);
    brush_set_face_texture(b, FACE_TOP, "grass_leaves");
    brush_set_face_scale(b, FACE_TOP, 0.5f, 0.5f);

    /* Entities */
    Entity* player = entity_spawn();
    strcpy(player->classname, "info_player_start");
    player->origin = VEC3(0, -200, 1);

    Entity* door = brush_entity_create("func_door", "office_door");
    door->origin = VEC3(504, 0, 0);
    door->flags = EF_SOLID;
    brush_entity_data(door)->spawn_origin = VEC3_ZERO;
    brush_entity_add_brush(door, VEC3(-4,-64,0), VEC3(4,64,100), "wood", 1);

    Entity* trig = brush_entity_create("trigger_multiple", "office_trigger");
    trig->origin = VEC3(480, 0, 0);
    trig->flags = 0;
    brush_entity_data(trig)->spawn_origin = VEC3_ZERO;
    brush_entity_add_brush(trig, VEC3(-50,-80,0), VEC3(80,80,72), "trigger", 0);
    entity_io_connect(trig, "OnTrigger", "office_door", "Open", "", 0, 0);
    entity_io_connect(trig, "OnTrigger", "office_door", "Close", "", 3.0f, 0);

    Entity* npc = entity_spawn();
    strcpy(npc->classname, "npc_citizen");
    strcpy(npc->targetname, "guard");
    npc->origin = VEC3(650, 0, 1);
    npc->flags = EF_SOLID;

    /* Save */
    MapInfo info;
    strcpy(info.title, "Origin Demo Map");
    strcpy(info.description, "Multi-room demo with props and traps.");
    if (map_save(output, &info))
        printf("\nSaved: %d brushes, %d entities\n", brush_count(), entity_count());
    brush_system_shutdown();
    entity_system_shutdown();
    return 0;
}
