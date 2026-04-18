/*
 * physics.c — ODE physics integration
 */
#include "physics.h"
#include <ode/ode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BODIES 128
#define MAX_CONTACTS 16
#define ODE_SCALE 0.025f  /* 1 game unit = 0.025 meters (40 units per meter) */
#define ODE_INV_SCALE 40.0f

static dWorldID      g_world;
static dSpaceID      g_space;
static dJointGroupID g_contacts;
static int           g_initialized = 0;

static dBodyID       g_bodies[MAX_BODIES];
static dGeomID       g_body_geoms[MAX_BODIES];
static int           g_body_kinematic[MAX_BODIES];
static int           g_body_count = 0;

/* Collision callback */
static void near_callback(void* data, dGeomID o1, dGeomID o2) {
    (void)data;
    dBodyID b1 = dGeomGetBody(o1);
    dBodyID b2 = dGeomGetBody(o2);

    /* Skip if both are static */
    if (!b1 && !b2) return;

    dContact contacts[MAX_CONTACTS];
    int n = dCollide(o1, o2, MAX_CONTACTS, &contacts[0].geom, sizeof(dContact));

    for (int i = 0; i < n; i++) {
        contacts[i].surface.mode = dContactSoftCFM | dContactSoftERP | dContactApprox1;
        contacts[i].surface.mu = 20.0;
        contacts[i].surface.soft_cfm = 0.002;
        contacts[i].surface.soft_erp = 0.15;

        dJointID c = dJointCreateContact(g_world, g_contacts, &contacts[i]);
        dJointAttach(c, b1, b2);
    }
}

int physics_init(void) {
    dInitODE();
    g_world = dWorldCreate();
    g_space = dHashSpaceCreate(0);
    g_contacts = dJointGroupCreate(0);

    dWorldSetGravity(g_world, 0, 0, -800 * ODE_SCALE); /* ~-20 m/s² in ODE scale */
    dWorldSetCFM(g_world, 1e-5);
    dWorldSetERP(g_world, 0.8);
    dWorldSetAutoDisableFlag(g_world, 1);
    dWorldSetAutoDisableLinearThreshold(g_world, 1.0);
    dWorldSetAutoDisableAngularThreshold(g_world, 1.0);
    dWorldSetAutoDisableSteps(g_world, 20);
    dWorldSetLinearDamping(g_world, 0.01);
    dWorldSetAngularDamping(g_world, 0.05);
    dWorldSetMaxAngularSpeed(g_world, 20.0);
    dWorldSetQuickStepNumIterations(g_world, 10);

    memset(g_bodies, 0, sizeof(g_bodies));
    memset(g_body_geoms, 0, sizeof(g_body_geoms));
    memset(g_body_kinematic, 0, sizeof(g_body_kinematic));
    g_body_count = 0;
    g_initialized = 1;

    printf("[physics] ODE initialized\n");
    return 1;
}

void physics_shutdown(void) {
    if (!g_initialized) return;
    dJointGroupDestroy(g_contacts);
    dSpaceDestroy(g_space);
    dWorldDestroy(g_world);
    dCloseODE();
    g_initialized = 0;
    printf("[physics] shutdown\n");
}

static float g_physics_accumulator = 0;
#define PHYSICS_STEP (1.0f / 60.0f)

void physics_tick(float dt) {
    if (!g_initialized) return;
    g_physics_accumulator += dt;
    /* Fixed timestep — max 3 steps per frame to avoid spiral of death */
    int steps = 0;
    while (g_physics_accumulator >= PHYSICS_STEP && steps < 3) {
        dSpaceCollide(g_space, NULL, near_callback);
        dWorldQuickStep(g_world, PHYSICS_STEP);
        dJointGroupEmpty(g_contacts);
        g_physics_accumulator -= PHYSICS_STEP;
        steps++;
    }
}

/* Add static box collider */
void physics_add_static_box(Vec3 mins, Vec3 maxs) {
    if (!g_initialized) return;
    float cx = (mins.x + maxs.x) * 0.5f * ODE_SCALE;
    float cy = (mins.y + maxs.y) * 0.5f * ODE_SCALE;
    float cz = (mins.z + maxs.z) * 0.5f * ODE_SCALE;
    float sx = (maxs.x - mins.x) * ODE_SCALE;
    float sy = (maxs.y - mins.y) * ODE_SCALE;
    float sz = (maxs.z - mins.z) * ODE_SCALE;
    if (sx <= 0 || sy <= 0 || sz <= 0) return;

    dGeomID geom = dCreateBox(g_space, sx, sy, sz);
    dGeomSetPosition(geom, cx, cy, cz);
}

/* Add static trimesh collider from brush face vertices (for non-AABB brushes) */
void physics_add_static_trimesh(const float* verts, int vert_count,
                                 const int* indices, int tri_count) {
    if (!g_initialized || vert_count == 0 || tri_count == 0) return;

    /* Copy and scale vertices — must persist for ODE */
    float* scaled = (float*)malloc(vert_count * 3 * sizeof(float));
    for (int i = 0; i < vert_count * 3; i++)
        scaled[i] = verts[i] * ODE_SCALE;

    /* Copy indices — must also persist for ODE */
    int* idx_copy = (int*)malloc(tri_count * 3 * sizeof(int));
    memcpy(idx_copy, indices, tri_count * 3 * sizeof(int));

    dTriMeshDataID data = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(data, scaled, 3 * sizeof(float), vert_count,
                                 idx_copy, tri_count * 3, 3 * sizeof(int));
    dGeomID geom = dCreateTriMesh(g_space, data, NULL, NULL, NULL);
    dGeomSetPosition(geom, 0, 0, 0);
}

int physics_create_body_box(Vec3 pos, Vec3 half_size, float mass) {
    if (!g_initialized || g_body_count >= MAX_BODIES) return -1;

    int id = g_body_count++;
    dBodyID body = dBodyCreate(g_world);
    dMass m;
    dMassSetBoxTotal(&m, mass, half_size.x*2*ODE_SCALE, half_size.y*2*ODE_SCALE, half_size.z*2*ODE_SCALE);
    dBodySetMass(body, &m);
    dBodySetPosition(body, pos.x*ODE_SCALE, pos.y*ODE_SCALE, pos.z*ODE_SCALE);

    dGeomID geom = dCreateBox(g_space, half_size.x*2*ODE_SCALE, half_size.y*2*ODE_SCALE, half_size.z*2*ODE_SCALE);
    dGeomSetBody(geom, body);

    g_bodies[id] = body;
    g_body_geoms[id] = geom;
    g_body_kinematic[id] = 0;
    return id;
}

int physics_create_body_cylinder(Vec3 pos, float radius, float height, float mass) {
    if (!g_initialized || g_body_count >= MAX_BODIES) return -1;

    int id = g_body_count++;
    dBodyID body = dBodyCreate(g_world);
    dMass m;
    dMassSetCylinderTotal(&m, mass, 3, radius*ODE_SCALE, height*ODE_SCALE);
    dBodySetMass(body, &m);
    dBodySetPosition(body, pos.x*ODE_SCALE, pos.y*ODE_SCALE, (pos.z + height*0.5f)*ODE_SCALE);

    dGeomID geom = dCreateCylinder(g_space, radius*ODE_SCALE, height*ODE_SCALE);
    dGeomSetBody(geom, body);

    g_bodies[id] = body;
    g_body_geoms[id] = geom;
    g_body_kinematic[id] = 0;
    return id;
}

int physics_create_body_mesh(Vec3 pos, const float* verts, int vert_count,
                              const unsigned int* indices, int tri_count, float mass) {
    if (!g_initialized || g_body_count >= MAX_BODIES) return -1;
    if (vert_count == 0 || tri_count == 0) return -1;

    int id = g_body_count++;

    /* Copy and scale vertices */
    float* sv = (float*)malloc(vert_count * 3 * sizeof(float));
    for (int i = 0; i < vert_count * 3; i++)
        sv[i] = verts[i] * ODE_SCALE;

    /* Copy indices as int (ODE wants dTriIndex which is int) */
    int* si = (int*)malloc(tri_count * 3 * sizeof(int));
    for (int i = 0; i < tri_count * 3; i++)
        si[i] = (int)indices[i];

    /* Compute AABB for mass approximation */
    float mnx=1e9,mny=1e9,mnz=1e9,mxx=-1e9,mxy=-1e9,mxz=-1e9;
    for (int i = 0; i < vert_count; i++) {
        if (sv[i*3] < mnx) mnx = sv[i*3];
        if (sv[i*3+1] < mny) mny = sv[i*3+1];
        if (sv[i*3+2] < mnz) mnz = sv[i*3+2];
        if (sv[i*3] > mxx) mxx = sv[i*3];
        if (sv[i*3+1] > mxy) mxy = sv[i*3+1];
        if (sv[i*3+2] > mxz) mxz = sv[i*3+2];
    }

    dBodyID body = dBodyCreate(g_world);
    dMass m;
    dMassSetBoxTotal(&m, mass, mxx-mnx, mxy-mny, mxz-mnz);
    dBodySetMass(body, &m);
    dBodySetPosition(body, pos.x * ODE_SCALE, pos.y * ODE_SCALE, pos.z * ODE_SCALE);

    dTriMeshDataID data = dGeomTriMeshDataCreate();
    dGeomTriMeshDataBuildSingle(data, sv, 3 * sizeof(float), vert_count,
                                 si, tri_count * 3, 3 * sizeof(int));
    dGeomID geom = dCreateTriMesh(g_space, data, NULL, NULL, NULL);
    dGeomSetBody(geom, body);

    g_bodies[id] = body;
    g_body_geoms[id] = geom;
    g_body_kinematic[id] = 0;
    return id;
}

Vec3 physics_body_get_pos(int id) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return VEC3_ZERO;
    const dReal* p = dBodyGetPosition(g_bodies[id]);
    return VEC3((float)p[0] * ODE_INV_SCALE, (float)p[1] * ODE_INV_SCALE, (float)p[2] * ODE_INV_SCALE);
}

void physics_body_get_rotation(int id, float out_mat[12]) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) {
        memset(out_mat, 0, 12 * sizeof(float));
        out_mat[0] = out_mat[5] = out_mat[10] = 1;
        return;
    }
    const dReal* r = dBodyGetRotation(g_bodies[id]);
    for (int i = 0; i < 12; i++) out_mat[i] = (float)r[i];
}

void physics_body_set_pos(int id, Vec3 pos) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return;
    dBodySetPosition(g_bodies[id], pos.x*ODE_SCALE, pos.y*ODE_SCALE, pos.z*ODE_SCALE);
}

void physics_body_set_vel(int id, Vec3 vel) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return;
    dBodySetLinearVel(g_bodies[id], vel.x*ODE_SCALE, vel.y*ODE_SCALE, vel.z*ODE_SCALE);
}

Vec3 physics_body_get_vel(int id) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return VEC3_ZERO;
    const dReal* v = dBodyGetLinearVel(g_bodies[id]);
    return VEC3((float)v[0]*ODE_INV_SCALE, (float)v[1]*ODE_INV_SCALE, (float)v[2]*ODE_INV_SCALE);
}

void physics_body_add_force(int id, Vec3 force) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return;
    dBodyEnable(g_bodies[id]);
    dBodyAddForce(g_bodies[id], force.x*ODE_SCALE, force.y*ODE_SCALE, force.z*ODE_SCALE);
}

void physics_body_set_kinematic(int id, int kinematic) {
    if (id < 0 || id >= g_body_count || !g_bodies[id]) return;
    g_body_kinematic[id] = kinematic;
    if (kinematic) {
        dBodySetKinematic(g_bodies[id]);
    } else {
        dBodySetDynamic(g_bodies[id]);
        dBodyEnable(g_bodies[id]);
    }
}

void physics_set_gravity(float gz) {
    if (!g_initialized) return;
    dWorldSetGravity(g_world, 0, 0, gz * ODE_SCALE);
}
