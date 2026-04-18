/*
 * render.c — Origin Engine Vulkan renderer
 *            Textured brushes + wireframe overlay + free camera
 */
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "camera.h"
#include "brush.h"
#include "player.h"
#include "texture.h"
#include "brush_entity.h"
#include "font.h"
#include "console.h"
#include "prop.h"
#include "physics.h"
#include "stb_image.h"

#define MAX_FRAMES_IN_FLIGHT 2
/* 6 faces * 2 tris * 3 verts = 36 verts per brush */
#define MAX_BRUSH_VERTS (256 * 128 + 128 * 512)  /* brushes + props */
#define MAX_WIRE_VERTS  (256 * 64)

/* Textured vertex */
typedef struct {
    float pos[3];
    float uv[2];
    float tex_index;
    float alpha;
    float normal[3];
} BrushVertex;

/* Wireframe vertex */
typedef struct {
    float pos[3];
    float col[3];
} WireVertex;

/* ── Vulkan State ────────────────────────────────────────────── */

static GLFWwindow*       g_window;
static VkInstance        g_instance;
static VkSurfaceKHR     g_surface;
static VkPhysicalDevice  g_physical_device;
static VkDevice          g_device;
static VkQueue           g_graphics_queue;
static VkQueue           g_present_queue;
static uint32_t          g_graphics_family;
static uint32_t          g_present_family;

static VkSwapchainKHR    g_swapchain;
static VkFormat          g_swapchain_format;
static VkExtent2D        g_swapchain_extent;
static uint32_t          g_image_count;
static VkImage*          g_swapchain_images;
static VkImageView*      g_swapchain_views;

static VkRenderPass      g_render_pass;

/* Brush pipeline (textured triangles) */
static VkPipelineLayout  g_brush_layout;
static VkPipeline        g_brush_pipeline;
static VkPipeline        g_brush_transparent_pipeline;
static VkDescriptorSetLayout g_desc_layout;
static VkDescriptorPool  g_desc_pool;
static VkDescriptorSet   g_desc_set;

/* Wireframe pipeline (lines overlay) */
static VkPipelineLayout  g_wire_layout;
static VkPipeline        g_wire_pipeline;

static VkFramebuffer*    g_framebuffers;
static VkCommandPool     g_command_pool;
static VkCommandBuffer   g_command_buffers[MAX_FRAMES_IN_FLIGHT];
static VkSemaphore       g_image_available[MAX_FRAMES_IN_FLIGHT];
static VkSemaphore       g_render_finished[MAX_FRAMES_IN_FLIGHT];
static VkFence           g_in_flight[MAX_FRAMES_IN_FLIGHT];
static int               g_current_frame = 0;

/* Vertex buffers */
static VkBuffer          g_brush_vb;
static VkDeviceMemory    g_brush_vb_mem;
static uint32_t          g_brush_vert_count;

static VkBuffer          g_wire_vb;
static VkDeviceMemory    g_wire_vb_mem;
static uint32_t          g_wire_vert_count;

/* Textures */
static VkImage*          g_tex_images;
static VkDeviceMemory*   g_tex_memories;
static VkImageView*      g_tex_views;
static VkSampler         g_sampler;
static int               g_tex_count;

/* Input state */
static double            g_last_mouse_x, g_last_mouse_y;
static int               g_mouse_captured = 1;
static int               g_show_wireframe = 0;
static Camera*           g_camera_override = NULL;

/* Opaque/transparent split counts */
static uint32_t          g_opaque_vert_count;
static uint32_t          g_transparent_vert_count;

/* Overlay pipeline */
static VkPipelineLayout  g_overlay_layout;
static VkPipeline        g_overlay_pipeline;

/* Skybox */
static VkPipelineLayout  g_sky_layout;
static VkPipeline        g_sky_pipeline;
static VkDescriptorSetLayout g_sky_desc_layout;
static VkDescriptorPool  g_sky_desc_pool;
static VkDescriptorSet   g_sky_desc_set;
static VkImage           g_sky_image;
static VkDeviceMemory    g_sky_memory;
static VkImageView       g_sky_view;
static VkSampler         g_sky_sampler;
static int               g_sky_loaded = 0;
static char              g_sky_pending_path[512] = "";
static int               g_draw_overlay = 0;
static float             g_overlay_color[4] = {0, 0, 0, 0.6f};

/* Text rendering */
static VkPipelineLayout  g_text_layout;
static VkPipeline        g_text_pipeline;
static VkDescriptorSetLayout g_text_desc_layout;
static VkDescriptorPool  g_text_desc_pool;
static VkDescriptorSet   g_text_desc_set;
static VkImage           g_font_image;
static VkDeviceMemory    g_font_memory;
static VkImageView       g_font_view;
static VkSampler         g_font_sampler;
static VkBuffer          g_text_vb;
static VkDeviceMemory    g_text_vb_mem;
static uint32_t          g_text_vert_count;
static char              g_screen_texts[64][128];
static float             g_screen_text_x[64];
static float             g_screen_text_y[64];
static float             g_screen_text_scale[64];
static float             g_screen_text_color[64][3];
static int               g_screen_text_count = 0;

/* Depth buffer */
static VkImage           g_depth_image;
static VkDeviceMemory    g_depth_memory;
static VkImageView       g_depth_view;
static VkFormat          g_depth_format = VK_FORMAT_D32_SFLOAT;

/* ── Helpers ─────────────────────────────────────────────────── */

static uint32_t* load_spirv(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) { printf("[render] can't open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t* buf = (uint32_t*)malloc(*size);
    fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

static void mat4_multiply(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c*4+r] = a[0*4+r]*b[c*4+0] + a[1*4+r]*b[c*4+1] +
                         a[2*4+r]*b[c*4+2] + a[3*4+r]*b[c*4+3];
}

static uint32_t find_memory_type(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g_physical_device, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1 << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return 0;
}

/* Forward declarations */
static void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buf, VkDeviceMemory* mem);
static VkCommandBuffer begin_single_cmd(void);
static void end_single_cmd(VkCommandBuffer cmd);
static void render_load_skybox_internal(void);

/* 4x4 matrix inverse (column-major) */
static void mat4_inverse(const float m[16], float out[16]) {
    float inv[16], det;
    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
    det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (fabsf(det) < 1e-10f) { memcpy(out, m, 64); return; }
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
}

static VkShaderModule create_shader_module(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = size, .pCode = code };
    VkShaderModule mod;
    vkCreateShaderModule(g_device, &ci, NULL, &mod);
    return mod;
}

/* ── Input callbacks ─────────────────────────────────────────── */

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (key == GLFW_KEY_V && action == GLFW_PRESS) {
        Entity* pe = player_get();
        PlayerData* pd = player_data(pe);
        if (pd) {
            pd->noclip = !pd->noclip;
            printf("[player] noclip %s\n", pd->noclip ? "ON" : "OFF");
        }
    }
    if (key == GLFW_KEY_Z && action == GLFW_PRESS) {
        g_show_wireframe = !g_show_wireframe;
        printf("[render] wireframe overlay %s\n", g_show_wireframe ? "ON" : "OFF");
    }
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !g_mouse_captured) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &g_last_mouse_x, &g_last_mouse_y);
        g_mouse_captured = 1;
    }
}

/* ── Build brush face geometry ───────────────────────────────── */

/* Texture scale: 1 texture repeat per N units */
#define TEX_SCALE (1.0f / 128.0f)

static void add_quad(BrushVertex* v, int* idx,
                     float ax, float ay, float az,
                     float bx, float by, float bz,
                     float cx, float cy, float cz,
                     float dx, float dy, float dz,
                     float u0, float v0, float u1, float v1,
                     float tex_index, float alpha) {
    /* Triangle 1: A B C */
    v[*idx] = (BrushVertex){{ax,ay,az},{u0,v0},tex_index,alpha}; (*idx)++;
    v[*idx] = (BrushVertex){{bx,by,bz},{u1,v0},tex_index,alpha}; (*idx)++;
    v[*idx] = (BrushVertex){{cx,cy,cz},{u1,v1},tex_index,alpha}; (*idx)++;
    /* Triangle 2: A C D */
    v[*idx] = (BrushVertex){{ax,ay,az},{u0,v0},tex_index,alpha}; (*idx)++;
    v[*idx] = (BrushVertex){{cx,cy,cz},{u1,v1},tex_index,alpha}; (*idx)++;
    v[*idx] = (BrushVertex){{dx,dy,dz},{u0,v1},tex_index,alpha}; (*idx)++;
}

/* pass: 0=opaque only, 1=transparent only, 2=all */
/* Compute UV for a vertex on a face using planar projection */
static void compute_face_uv(const BrushFace* bf, Vec3 vert,
                             float* out_u, float* out_v) {
    /* Use local vertex position so textures move with the brush */
    Vec3 p = vert;
    Vec3 n = bf->normal;
    float ax = fabsf(n.x), ay = fabsf(n.y), az = fabsf(n.z);
    float su = bf->scale_u > 0.0f ? bf->scale_u : 1.0f;
    float sv = bf->scale_v > 0.0f ? bf->scale_v : 1.0f;

    /* Project onto the dominant plane */
    if (az >= ax && az >= ay) {
        /* Z-dominant: project onto XY */
        *out_u = p.x * TEX_SCALE / su + bf->offset_u;
        *out_v = p.y * TEX_SCALE / sv + bf->offset_v;
    } else if (ay >= ax) {
        /* Y-dominant: project onto XZ */
        *out_u = p.x * TEX_SCALE / su + bf->offset_u;
        *out_v = p.z * TEX_SCALE / sv + bf->offset_v;
    } else {
        /* X-dominant: project onto YZ */
        *out_u = p.y * TEX_SCALE / su + bf->offset_u;
        *out_v = p.z * TEX_SCALE / sv + bf->offset_v;
    }
}

/* Build triangles for one brush — supports arbitrary face polygons via fan triangulation */
static void build_one_brush(BrushVertex* verts, int* count, Brush* b, Vec3 offset, int pass) {
    for (int f = 0; f < b->face_count; f++) {
        BrushFace* bf = &b->faces[f];
        if (bf->vertex_count < 3) continue;

        int tex_idx = texture_find(bf->texture);
        const TextureInfo* ti = tex_idx >= 0 ? texture_get_info(tex_idx) : NULL;
        if (ti && ti->nodraw) continue;

        float fidx = tex_idx >= 0 ? (float)tex_idx : 0.0f;
        int tool = (ti && ti->is_tool);

        /* Hide tool textures unless brush_showtriggers is on */
        if (tool && cvar_get("brush_showtriggers") < 1.0f) continue;

        /* Hide nodraw unless brush_showinvis is on */
        if (ti && ti->nodraw && cvar_get("brush_showinvis") < 1.0f) continue;

        float alpha = tool ? 0.4f : 1.0f;
        int is_transparent = (alpha < 1.0f);

        if (pass == 0 && is_transparent) continue;
        if (pass == 1 && !is_transparent) continue;

        /* Fan triangulation: vertex 0 is the hub */
        for (int v = 1; v < bf->vertex_count - 1; v++) {
            Vec3 v0 = bf->vertices[0];
            Vec3 v1 = bf->vertices[v];
            Vec3 v2 = bf->vertices[v + 1];

            float u0, uv0, u1, uv1, u2, uv2;
            compute_face_uv(bf, v0, &u0, &uv0);
            compute_face_uv(bf, v1, &u1, &uv1);
            compute_face_uv(bf, v2, &u2, &uv2);

            Vec3 n = bf->normal;
            verts[*count] = (BrushVertex){{v0.x+offset.x, v0.y+offset.y, v0.z+offset.z},
                                           {u0, uv0}, fidx, alpha, {n.x,n.y,n.z}}; (*count)++;
            verts[*count] = (BrushVertex){{v1.x+offset.x, v1.y+offset.y, v1.z+offset.z},
                                           {u1, uv1}, fidx, alpha, {n.x,n.y,n.z}}; (*count)++;
            verts[*count] = (BrushVertex){{v2.x+offset.x, v2.y+offset.y, v2.z+offset.z},
                                           {u2, uv2}, fidx, alpha, {n.x,n.y,n.z}}; (*count)++;
        }
    }
}

static void build_brush_faces(BrushVertex* verts, int* count) {
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    *count = 0;

    for (int i = 0; i < bcount; i++) {
        if (!brushes[i] || brushes[i]->entity_owned) continue;
        build_one_brush(verts, count, brushes[i], VEC3_ZERO, 2);
    }
}

/* ── Build wireframe vertices (same as before) ───────────────── */

static void add_line(WireVertex* v, int* idx, float x0, float y0, float z0,
                     float x1, float y1, float z1, float r, float g, float b) {
    v[*idx].pos[0]=x0; v[*idx].pos[1]=y0; v[*idx].pos[2]=z0;
    v[*idx].col[0]=r;  v[*idx].col[1]=g;  v[*idx].col[2]=b; (*idx)++;
    v[*idx].pos[0]=x1; v[*idx].pos[1]=y1; v[*idx].pos[2]=z1;
    v[*idx].col[0]=r;  v[*idx].col[1]=g;  v[*idx].col[2]=b; (*idx)++;
}

static void build_wire_vertices(WireVertex* verts, int* count) {
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    *count = 0;
    for (int i = 0; i < bcount; i++) {
        Brush* b = brushes[i];
        if (!b) continue;
        float r=1,g=1,bl=0;
        /* Draw edges of each face */
        for (int f = 0; f < b->face_count; f++) {
            BrushFace* bf = &b->faces[f];
            for (int v = 0; v < bf->vertex_count; v++) {
                Vec3 a = bf->vertices[v];
                Vec3 bn = bf->vertices[(v+1) % bf->vertex_count];
                add_line(verts, count, a.x,a.y,a.z, bn.x,bn.y,bn.z, r,g,bl);
            }
        }
    }
}

/* ── Vulkan setup functions ──────────────────────────────────── */

static int create_instance(void) {
    VkApplicationInfo ai = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Origin Engine", .applicationVersion = VK_MAKE_VERSION(1,0,0),
        .pEngineName = "Origin Engine", .engineVersion = VK_MAKE_VERSION(1,0,0),
        .apiVersion = VK_API_VERSION_1_0,
    };
    uint32_t ec; const char** exts = glfwGetRequiredInstanceExtensions(&ec);
    VkInstanceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &ai, .enabledExtensionCount = ec, .ppEnabledExtensionNames = exts };
    return vkCreateInstance(&ci, NULL, &g_instance) == VK_SUCCESS;
}

static int pick_physical_device(void) {
    uint32_t c = 0;
    vkEnumeratePhysicalDevices(g_instance, &c, NULL);
    if (!c) return 0;
    VkPhysicalDevice* d = malloc(c * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_instance, &c, d);
    g_physical_device = d[0];
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(g_physical_device, &p);
    printf("[render] GPU: %s\n", p.deviceName);
    free(d);

    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &qc, NULL);
    VkQueueFamilyProperties* qp = malloc(qc * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(g_physical_device, &qc, qp);
    int fg=0, fp=0;
    for (uint32_t i = 0; i < qc; i++) {
        if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_graphics_family=i; fg=1; }
        VkBool32 ps=0; vkGetPhysicalDeviceSurfaceSupportKHR(g_physical_device,i,g_surface,&ps);
        if (ps) { g_present_family=i; fp=1; }
        if (fg&&fp) break;
    }
    free(qp);
    return fg && fp;
}

static int create_device(void) {
    float pri = 1.0f;
    uint32_t fam[2] = {g_graphics_family, g_present_family};
    int fc = (g_graphics_family==g_present_family)?1:2;
    VkDeviceQueueCreateInfo qci[2];
    for (int i=0;i<fc;i++)
        qci[i] = (VkDeviceQueueCreateInfo){.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex=fam[i],.queueCount=1,.pQueuePriorities=&pri};

    VkPhysicalDeviceFeatures feat = {.wideLines=VK_TRUE,.fillModeNonSolid=VK_TRUE,.samplerAnisotropy=VK_TRUE};
    const char* ext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo ci = {.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=(uint32_t)fc,.pQueueCreateInfos=qci,
        .enabledExtensionCount=1,.ppEnabledExtensionNames=ext,.pEnabledFeatures=&feat};
    if (vkCreateDevice(g_physical_device,&ci,NULL,&g_device)!=VK_SUCCESS) return 0;
    vkGetDeviceQueue(g_device,g_graphics_family,0,&g_graphics_queue);
    vkGetDeviceQueue(g_device,g_present_family,0,&g_present_queue);
    return 1;
}

static int create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device,g_surface,&caps);
    uint32_t fc; vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device,g_surface,&fc,NULL);
    VkSurfaceFormatKHR* fmts=malloc(fc*sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device,g_surface,&fc,fmts);
    g_swapchain_format=fmts[0].format; VkColorSpaceKHR cs=fmts[0].colorSpace;
    for(uint32_t i=0;i<fc;i++) if(fmts[i].format==VK_FORMAT_B8G8R8A8_SRGB){g_swapchain_format=fmts[i].format;cs=fmts[i].colorSpace;break;}
    free(fmts);
    g_swapchain_extent=caps.currentExtent;
    if(g_swapchain_extent.width==UINT32_MAX){int w,h;glfwGetFramebufferSize(g_window,&w,&h);g_swapchain_extent.width=(uint32_t)w;g_swapchain_extent.height=(uint32_t)h;}
    g_image_count=caps.minImageCount+1;
    if(caps.maxImageCount>0&&g_image_count>caps.maxImageCount)g_image_count=caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=g_surface,
        .minImageCount=g_image_count,.imageFormat=g_swapchain_format,.imageColorSpace=cs,
        .imageExtent=g_swapchain_extent,.imageArrayLayers=1,.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform=caps.currentTransform,.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE};
    if(g_graphics_family!=g_present_family){uint32_t idx[]={g_graphics_family,g_present_family};
        ci.imageSharingMode=VK_SHARING_MODE_CONCURRENT;ci.queueFamilyIndexCount=2;ci.pQueueFamilyIndices=idx;}
    if(vkCreateSwapchainKHR(g_device,&ci,NULL,&g_swapchain)!=VK_SUCCESS)return 0;
    vkGetSwapchainImagesKHR(g_device,g_swapchain,&g_image_count,NULL);
    g_swapchain_images=malloc(g_image_count*sizeof(VkImage));
    vkGetSwapchainImagesKHR(g_device,g_swapchain,&g_image_count,g_swapchain_images);
    g_swapchain_views=malloc(g_image_count*sizeof(VkImageView));
    for(uint32_t i=0;i<g_image_count;i++){
        VkImageViewCreateInfo v={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=g_swapchain_images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=g_swapchain_format,
            .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
        vkCreateImageView(g_device,&v,NULL,&g_swapchain_views[i]);}
    return 1;
}

static int create_render_pass(void) {
    VkAttachmentDescription atts[2];
    /* Color attachment */
    atts[0] = (VkAttachmentDescription){.format=g_swapchain_format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    /* Depth attachment */
    atts[1] = (VkAttachmentDescription){.format=g_depth_format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference color_ref={.attachment=0,.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_ref={.attachment=1,.layout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=1,.pColorAttachments=&color_ref,.pDepthStencilAttachment=&depth_ref};
    VkSubpassDependency dep={.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,
        .srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo ci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=2,.pAttachments=atts,.subpassCount=1,.pSubpasses=&sub,
        .dependencyCount=1,.pDependencies=&dep};
    return vkCreateRenderPass(g_device,&ci,NULL,&g_render_pass)==VK_SUCCESS;
}

static int create_depth_buffer(void) {
    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=g_depth_format,.extent={.width=g_swapchain_extent.width,.height=g_swapchain_extent.height,.depth=1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
    vkCreateImage(g_device,&ici,NULL,&g_depth_image);

    VkMemoryRequirements req; vkGetImageMemoryRequirements(g_device,g_depth_image,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    vkAllocateMemory(g_device,&ai,NULL,&g_depth_memory);
    vkBindImageMemory(g_device,g_depth_image,g_depth_memory,0);

    VkImageViewCreateInfo vci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=g_depth_image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=g_depth_format,
        .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT,.levelCount=1,.layerCount=1}};
    vkCreateImageView(g_device,&vci,NULL,&g_depth_view);
    return 1;
}

/* ── Texture upload to Vulkan ────────────────────────────────── */

static VkCommandBuffer begin_single_cmd(void) {
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=g_command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(g_device,&ai,&cmd);
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cmd,&bi);
    return cmd;
}

static void end_single_cmd(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    vkQueueSubmit(g_graphics_queue,1,&si,VK_NULL_HANDLE);
    vkQueueWaitIdle(g_graphics_queue);
    vkFreeCommandBuffers(g_device,g_command_pool,1,&cmd);
}

static int upload_textures(void) {
    g_tex_count = texture_count();
    if (g_tex_count == 0) {
        printf("[render] no textures to upload\n");
        return 1;
    }

    g_tex_images = calloc(g_tex_count, sizeof(VkImage));
    g_tex_memories = calloc(g_tex_count, sizeof(VkDeviceMemory));
    g_tex_views = calloc(g_tex_count, sizeof(VkImageView));

    for (int i = 0; i < g_tex_count; i++) {
        const TextureInfo* ti = texture_get_info(i);
        unsigned char* pixels = texture_get_pixels(i);
        if (!pixels) continue;

        VkDeviceSize img_size = (VkDeviceSize)(ti->width * ti->height * 4);

        /* Staging buffer */
        VkBuffer staging; VkDeviceMemory staging_mem;
        VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=img_size,
            .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        vkCreateBuffer(g_device,&bci,NULL,&staging);
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(g_device,staging,&req);
        VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
            .memoryTypeIndex=find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        vkAllocateMemory(g_device,&mai,NULL,&staging_mem);
        vkBindBufferMemory(g_device,staging,staging_mem,0);
        void* data; vkMapMemory(g_device,staging_mem,0,img_size,0,&data);
        memcpy(data,pixels,img_size);
        vkUnmapMemory(g_device,staging_mem);

        /* Create image */
        VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
            .format=VK_FORMAT_R8G8B8A8_SRGB,.extent={.width=(uint32_t)ti->width,.height=(uint32_t)ti->height,.depth=1},
            .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
        vkCreateImage(g_device,&ici,NULL,&g_tex_images[i]);
        VkMemoryRequirements ireq; vkGetImageMemoryRequirements(g_device,g_tex_images[i],&ireq);
        VkMemoryAllocateInfo imai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=ireq.size,
            .memoryTypeIndex=find_memory_type(ireq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        vkAllocateMemory(g_device,&imai,NULL,&g_tex_memories[i]);
        vkBindImageMemory(g_device,g_tex_images[i],g_tex_memories[i],0);

        /* Transition + copy */
        VkCommandBuffer cmd = begin_single_cmd();
        VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=g_tex_images[i],.subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1},
            .srcAccessMask=0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&bar);
        VkBufferImageCopy region={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},
            .imageExtent={.width=(uint32_t)ti->width,.height=(uint32_t)ti->height,.depth=1}};
        vkCmdCopyBufferToImage(cmd,staging,g_tex_images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&bar);
        end_single_cmd(cmd);

        vkDestroyBuffer(g_device,staging,NULL);
        vkFreeMemory(g_device,staging_mem,NULL);

        /* Image view */
        VkImageViewCreateInfo vci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=g_tex_images[i],
            .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_SRGB,
            .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
        vkCreateImageView(g_device,&vci,NULL,&g_tex_views[i]);
    }

    /* Sampler */
    VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeV=VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW=VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable=VK_TRUE,.maxAnisotropy=16.0f,
        .borderColor=VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_LINEAR};
    vkCreateSampler(g_device,&sci,NULL,&g_sampler);

    printf("[render] uploaded %d textures to GPU\n", g_tex_count);
    return 1;
}

/* ── Descriptor set for textures ─────────────────────────────── */

static int create_descriptors(void) {
    int count = g_tex_count > 0 ? g_tex_count : 1;

    /* Layout: array of combined image samplers */
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount=(uint32_t)count,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&binding};
    vkCreateDescriptorSetLayout(g_device,&lci,NULL,&g_desc_layout);

    /* Pool */
    VkDescriptorPoolSize ps={.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=(uint32_t)count};
    VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    vkCreateDescriptorPool(g_device,&pci,NULL,&g_desc_pool);

    /* Allocate set */
    VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=g_desc_pool,.descriptorSetCount=1,.pSetLayouts=&g_desc_layout};
    vkAllocateDescriptorSets(g_device,&ai,&g_desc_set);

    /* Write texture descriptors */
    VkDescriptorImageInfo* img_infos = calloc(count, sizeof(VkDescriptorImageInfo));
    for (int i = 0; i < count; i++) {
        img_infos[i].sampler = g_sampler;
        img_infos[i].imageView = (i < g_tex_count) ? g_tex_views[i] : g_tex_views[0];
        img_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=g_desc_set,.dstBinding=0,.descriptorCount=(uint32_t)count,
        .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=img_infos};
    vkUpdateDescriptorSets(g_device,1,&write,0,NULL);
    free(img_infos);
    return 1;
}

/* ── Pipelines ───────────────────────────────────────────────── */

static int create_brush_pipeline(void) {
    size_t vs,fs;
    uint32_t* vc=load_spirv("engine/shaders/brush_vert.spv",&vs);
    uint32_t* fc=load_spirv("engine/shaders/brush_frag.spv",&fs);
    if(!vc||!fc)return 0;
    VkShaderModule vm=create_shader_module(vc,vs), fm=create_shader_module(fc,fs);
    free(vc);free(fc);

    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};

    VkVertexInputBindingDescription bind={.binding=0,.stride=sizeof(BrushVertex),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[]={
        {.location=0,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(BrushVertex,pos)},
        {.location=1,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,.offset=offsetof(BrushVertex,uv)},
        {.location=2,.binding=0,.format=VK_FORMAT_R32_SFLOAT,.offset=offsetof(BrushVertex,tex_index)},
        {.location=3,.binding=0,.format=VK_FORMAT_R32_SFLOAT,.offset=offsetof(BrushVertex,alpha)},
        {.location=4,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(BrushVertex,normal)}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&bind,
        .vertexAttributeDescriptionCount=5,.pVertexAttributeDescriptions=attrs};

    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp={.width=(float)g_swapchain_extent.width,.height=(float)g_swapchain_extent.height,.maxDepth=1.0f};
    VkRect2D sc={.extent=g_swapchain_extent};
    VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1.0f,.cullMode=VK_CULL_MODE_BACK_BIT,.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};

    /* Alpha blending for transparent tool textures */
    VkPipelineColorBlendAttachmentState ba={
        .blendEnable=VK_TRUE,
        .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp=VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp=VK_BLEND_OP_ADD,
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&ba};

    /* Depth testing */
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,
        .depthCompareOp=VK_COMPARE_OP_LESS,.depthBoundsTestEnable=VK_FALSE,.stencilTestEnable=VK_FALSE};

    VkPushConstantRange pcr={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT,.size=64};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&g_desc_layout,
        .pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
    vkCreatePipelineLayout(g_device,&li,NULL,&g_brush_layout);

    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vps,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,
        .pDepthStencilState=&ds,
        .layout=g_brush_layout,.renderPass=g_render_pass};
    int ok=vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_brush_pipeline)==VK_SUCCESS;

    /* Create transparent variant: same but no depth write */
    ds.depthWriteEnable = VK_FALSE;
    pi.pDepthStencilState = &ds;
    ok = ok && (vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_brush_transparent_pipeline)==VK_SUCCESS);

    vkDestroyShaderModule(g_device,vm,NULL);
    vkDestroyShaderModule(g_device,fm,NULL);
    return ok;
}

static int create_wire_pipeline(void) {
    size_t vs,fs;
    uint32_t* vc=load_spirv("engine/shaders/wire_vert.spv",&vs);
    uint32_t* fc=load_spirv("engine/shaders/wire_frag.spv",&fs);
    if(!vc||!fc)return 0;
    VkShaderModule vm=create_shader_module(vc,vs), fm=create_shader_module(fc,fs);
    free(vc);free(fc);

    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};

    VkVertexInputBindingDescription bind={.binding=0,.stride=sizeof(WireVertex),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[]={
        {.location=0,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(WireVertex,pos)},
        {.location=1,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(WireVertex,col)}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&bind,
        .vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=attrs};

    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST};
    VkViewport vp={.width=(float)g_swapchain_extent.width,.height=(float)g_swapchain_extent.height,.maxDepth=1.0f};
    VkRect2D sc={.extent=g_swapchain_extent};
    VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=2.0f,.cullMode=VK_CULL_MODE_NONE};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};

    /* Depth test but no write — wireframe draws on top of surfaces at same depth */
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_FALSE,
        .depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL,.depthBoundsTestEnable=VK_FALSE,.stencilTestEnable=VK_FALSE};

    VkPushConstantRange pcr={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT,.size=64};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
    vkCreatePipelineLayout(g_device,&li,NULL,&g_wire_layout);

    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vps,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,
        .pDepthStencilState=&ds,
        .layout=g_wire_layout,.renderPass=g_render_pass};
    int ok=vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_wire_pipeline)==VK_SUCCESS;
    vkDestroyShaderModule(g_device,vm,NULL);
    vkDestroyShaderModule(g_device,fm,NULL);
    return ok;
}

static int create_overlay_pipeline(void) {
    size_t vs,fs;
    uint32_t* vc=load_spirv("engine/shaders/overlay_vert.spv",&vs);
    uint32_t* fc=load_spirv("engine/shaders/overlay_frag.spv",&fs);
    if(!vc||!fc)return 0;
    VkShaderModule vm=create_shader_module(vc,vs), fm=create_shader_module(fc,fs);
    free(vc);free(fc);

    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};

    /* No vertex input — fullscreen triangle from vertex shader */
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp={.width=(float)g_swapchain_extent.width,.height=(float)g_swapchain_extent.height,.maxDepth=1.0f};
    VkRect2D sc={.extent=g_swapchain_extent};
    VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1.0f,.cullMode=VK_CULL_MODE_NONE};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};

    VkPipelineColorBlendAttachmentState ba={
        .blendEnable=VK_TRUE,
        .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp=VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp=VK_BLEND_OP_ADD,
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&ba};

    /* No depth test — always on top */
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_FALSE,.depthWriteEnable=VK_FALSE};

    /* Push constant: just a vec4 color (16 bytes) */
    VkPushConstantRange pcr={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT,.size=16};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
    vkCreatePipelineLayout(g_device,&li,NULL,&g_overlay_layout);

    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vps,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,
        .pDepthStencilState=&ds,
        .layout=g_overlay_layout,.renderPass=g_render_pass};
    int ok=vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_overlay_pipeline)==VK_SUCCESS;
    vkDestroyShaderModule(g_device,vm,NULL);
    vkDestroyShaderModule(g_device,fm,NULL);
    return ok;
}

static int create_text_pipeline(void) {
    /* Upload font atlas */
    unsigned char* pixels; int fw, fh;
    font_get_atlas(&pixels, &fw, &fh);

    VkDeviceSize img_size = (VkDeviceSize)(fw * fh * 4);
    VkBuffer staging; VkDeviceMemory staging_mem;
    VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=img_size,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    vkCreateBuffer(g_device,&bci,NULL,&staging);
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(g_device,staging,&req);
    VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    vkAllocateMemory(g_device,&mai,NULL,&staging_mem);
    vkBindBufferMemory(g_device,staging,staging_mem,0);
    void* data; vkMapMemory(g_device,staging_mem,0,img_size,0,&data);
    memcpy(data,pixels,img_size);
    vkUnmapMemory(g_device,staging_mem);

    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_SRGB,.extent={.width=(uint32_t)fw,.height=(uint32_t)fh,.depth=1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    vkCreateImage(g_device,&ici,NULL,&g_font_image);
    VkMemoryRequirements ireq; vkGetImageMemoryRequirements(g_device,g_font_image,&ireq);
    VkMemoryAllocateInfo imai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=ireq.size,
        .memoryTypeIndex=find_memory_type(ireq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    vkAllocateMemory(g_device,&imai,NULL,&g_font_memory);
    vkBindImageMemory(g_device,g_font_image,g_font_memory,0);

    VkCommandBuffer cmd = begin_single_cmd();
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=g_font_image,.subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1},
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&bar);
    VkBufferImageCopy region={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},
        .imageExtent={.width=(uint32_t)fw,.height=(uint32_t)fh,.depth=1}};
    vkCmdCopyBufferToImage(cmd,staging,g_font_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
    bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&bar);
    end_single_cmd(cmd);
    vkDestroyBuffer(g_device,staging,NULL); vkFreeMemory(g_device,staging_mem,NULL);

    VkImageViewCreateInfo vci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=g_font_image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
    vkCreateImageView(g_device,&vci,NULL,&g_font_view);

    VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    vkCreateSampler(g_device,&sci,NULL,&g_font_sampler);

    /* Descriptor set for font */
    VkDescriptorSetLayoutBinding bind={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dlci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&bind};
    vkCreateDescriptorSetLayout(g_device,&dlci,NULL,&g_text_desc_layout);

    VkDescriptorPoolSize ps={.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1};
    VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    vkCreateDescriptorPool(g_device,&dpci,NULL,&g_text_desc_pool);

    VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=g_text_desc_pool,.descriptorSetCount=1,.pSetLayouts=&g_text_desc_layout};
    vkAllocateDescriptorSets(g_device,&dsai,&g_text_desc_set);

    VkDescriptorImageInfo dii={.sampler=g_font_sampler,.imageView=g_font_view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wds={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g_text_desc_set,
        .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&dii};
    vkUpdateDescriptorSets(g_device,1,&wds,0,NULL);

    /* Text vertex buffer */
    create_buffer(MAX_TEXT_CHARS*6*sizeof(TextVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &g_text_vb, &g_text_vb_mem);

    /* Pipeline */
    size_t vs,fs;
    uint32_t* vc=load_spirv("engine/shaders/text_vert.spv",&vs);
    uint32_t* fc=load_spirv("engine/shaders/text_frag.spv",&fs);
    if(!vc||!fc)return 0;
    VkShaderModule vm=create_shader_module(vc,vs), fm=create_shader_module(fc,fs);
    free(vc);free(fc);

    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};

    VkVertexInputBindingDescription vbind={.binding=0,.stride=sizeof(TextVertex),.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vattrs[]={
        {.location=0,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,.offset=offsetof(TextVertex,pos)},
        {.location=1,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,.offset=offsetof(TextVertex,uv)},
        {.location=2,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(TextVertex,col)}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vbind,
        .vertexAttributeDescriptionCount=3,.pVertexAttributeDescriptions=vattrs};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp={.width=(float)g_swapchain_extent.width,.height=(float)g_swapchain_extent.height,.maxDepth=1.0f};
    VkRect2D sc={.extent=g_swapchain_extent};
    VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1.0f,.cullMode=VK_CULL_MODE_NONE};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.blendEnable=VK_TRUE,
        .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp=VK_BLEND_OP_ADD,.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp=VK_BLEND_OP_ADD,
        .colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_FALSE,.depthWriteEnable=VK_FALSE};

    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&g_text_desc_layout};
    vkCreatePipelineLayout(g_device,&pli,NULL,&g_text_layout);

    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vps,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,
        .pDepthStencilState=&ds,.layout=g_text_layout,.renderPass=g_render_pass};
    int ok=vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_text_pipeline)==VK_SUCCESS;
    vkDestroyShaderModule(g_device,vm,NULL);
    vkDestroyShaderModule(g_device,fm,NULL);
    return ok;
}



/* ── Framebuffers, Command Pool, Sync, Vertex Buffers ────────── */

static int create_framebuffers(void) {
    g_framebuffers=malloc(g_image_count*sizeof(VkFramebuffer));
    for(uint32_t i=0;i<g_image_count;i++){
        VkImageView attachments[2] = { g_swapchain_views[i], g_depth_view };
        VkFramebufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass=g_render_pass,.attachmentCount=2,.pAttachments=attachments,
            .width=g_swapchain_extent.width,.height=g_swapchain_extent.height,.layers=1};
        vkCreateFramebuffer(g_device,&ci,NULL,&g_framebuffers[i]);}
    return 1;
}

static int create_command_pool(void) {
    VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=g_graphics_family};
    vkCreateCommandPool(g_device,&ci,NULL,&g_command_pool);
    VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=g_command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=MAX_FRAMES_IN_FLIGHT};
    vkAllocateCommandBuffers(g_device,&ai,g_command_buffers);
    return 1;
}

static int create_sync(void) {
    VkSemaphoreCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};
    for(int i=0;i<MAX_FRAMES_IN_FLIGHT;i++){
        vkCreateSemaphore(g_device,&sci,NULL,&g_image_available[i]);
        vkCreateSemaphore(g_device,&sci,NULL,&g_render_finished[i]);
        vkCreateFence(g_device,&fci,NULL,&g_in_flight[i]);}
    return 1;
}

static void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buf, VkDeviceMemory* mem) {
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    vkCreateBuffer(g_device,&bi,NULL,buf);
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(g_device,*buf,&req);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    vkAllocateMemory(g_device,&ai,NULL,mem);
    vkBindBufferMemory(g_device,*buf,*mem,0);
}

static int create_vertex_buffers(void) {
    create_buffer(MAX_BRUSH_VERTS*sizeof(BrushVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &g_brush_vb, &g_brush_vb_mem);
    create_buffer(MAX_WIRE_VERTS*sizeof(WireVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &g_wire_vb, &g_wire_vb_mem);
    return 1;
}

/* ── Public API ──────────────────────────────────────────────── */

int render_init(int width, int height, const char* title) {
    if (!glfwInit()) return 0;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    g_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!g_window) return 0;

    glfwSetKeyCallback(g_window, key_callback);
    glfwSetMouseButtonCallback(g_window, mouse_button_callback);
    /* Mouse starts uncaptured — main.c manages capture state */
    glfwGetCursorPos(g_window, &g_last_mouse_x, &g_last_mouse_y);

    if (!create_instance()) return 0;
    if (glfwCreateWindowSurface(g_instance, g_window, NULL, &g_surface) != VK_SUCCESS) return 0;
    if (!pick_physical_device()) return 0;
    if (!create_device()) return 0;
    if (!create_swapchain()) return 0;
    if (!create_render_pass()) return 0;
    if (!create_depth_buffer()) return 0;
    if (!create_framebuffers()) return 0;
    if (!create_command_pool()) return 0;
    if (!create_sync()) return 0;
    if (!create_vertex_buffers()) return 0;

    /* Upload textures and create pipelines */
    if (!upload_textures()) return 0;
    if (!create_descriptors()) return 0;
    if (!create_brush_pipeline()) return 0;
    if (!create_wire_pipeline()) return 0;
    if (!create_overlay_pipeline()) return 0;
    if (!create_text_pipeline()) return 0;

    printf("[render] Origin Engine initialized (%dx%d)\n", width, height);
    return 1;
}

static void render_update_brushes_silent(void);

void render_update_brushes(void) {
    render_update_brushes_silent();

    /* Upload wireframe geometry */
    WireVertex* wv = malloc(MAX_WIRE_VERTS * sizeof(WireVertex));
    int wc = 0;
    build_wire_vertices(wv, &wc);
    g_wire_vert_count = (uint32_t)wc;
    void* data;
    vkMapMemory(g_device, g_wire_vb_mem, 0, wc * sizeof(WireVertex), 0, &data);
    memcpy(data, wv, wc * sizeof(WireVertex));
    vkUnmapMemory(g_device, g_wire_vb_mem);
    free(wv);

    printf("[render] uploaded %d opaque + %d transparent brush verts, %d wire verts\n",
           g_opaque_vert_count, g_transparent_vert_count, g_wire_vert_count);
}

Camera* render_get_camera(void) {
    return player_get_camera(player_get());
}

void render_process_input(float dt) {
    Entity* pe = player_get();
    PlayerData* pd = player_data(pe);
    if (!pe || !pd) return;

    /* Only process mouse if cursor is disabled (captured) */
    int cursor_mode = glfwGetInputMode(g_window, GLFW_CURSOR);
    if (cursor_mode == GLFW_CURSOR_DISABLED) {
        double mx, my;
        glfwGetCursorPos(g_window, &mx, &my);
        player_mouse_look(pe, (float)(mx - g_last_mouse_x), (float)(my - g_last_mouse_y));
        g_last_mouse_x = mx; g_last_mouse_y = my;
    }

    float fwd=0, right=0; int jump=0, crouch=0;
    if (glfwGetKey(g_window,GLFW_KEY_W)==GLFW_PRESS) fwd+=1;
    if (glfwGetKey(g_window,GLFW_KEY_S)==GLFW_PRESS) fwd-=1;
    if (glfwGetKey(g_window,GLFW_KEY_D)==GLFW_PRESS) right+=1;
    if (glfwGetKey(g_window,GLFW_KEY_A)==GLFW_PRESS) right-=1;
    if (glfwGetKey(g_window,GLFW_KEY_SPACE)==GLFW_PRESS) jump=1;
    if (glfwGetKey(g_window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS) crouch=1;
    if (glfwGetKey(g_window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS) pd->move_speed=500.0f; else pd->move_speed=250.0f;
    if (pd->noclip && crouch) { jump=-1; crouch=0; } /* ctrl = down in noclip */

    player_set_input(pe, fwd, right, jump, crouch);
    player_update(pe, dt);

    /* E key: grab/release physics props */
    static int prev_e = 0;
    int e_key = glfwGetKey(g_window, GLFW_KEY_E);
    if (e_key == GLFW_PRESS && !prev_e) {
        if (prop_is_holding()) {
            prop_release(0); /* drop */
        } else {
            prop_try_grab();
        }
    }
    prev_e = (e_key == GLFW_PRESS);

    /* Right click while holding: throw */
    static int prev_rmb = 0;
    int rmb = glfwGetMouseButton(g_window, GLFW_MOUSE_BUTTON_LEFT);
    if (rmb == GLFW_PRESS && !prev_rmb && prop_is_holding()) {
        prop_release(1); /* throw */
    }
    prev_rmb = (rmb == GLFW_PRESS);

    /* Update held prop position */
    if (prop_is_holding()) {
        Vec3 eye = pe->origin;
        eye.z += pd->eye_height;
        Vec3 fwd_dir = camera_forward(&pd->camera);
        prop_update_held(eye, fwd_dir);
    }
}

/* Build prop (mesh) vertices — always opaque, pass 0 only */
static void build_prop_vertices(BrushVertex* verts, int* count) {
    int pcount;
    Prop* props = prop_get_all(&pcount);

    for (int p = 0; p < pcount; p++) {
        Prop* pr = &props[p];
        if (!pr->active || !pr->mesh) continue;

        Mesh* m = pr->mesh;
        int tex_idx = texture_find(m->texture);
        float fidx = tex_idx >= 0 ? (float)tex_idx : 0.0f;
        float sc = pr->scale;

        /* Get rotation matrix from ODE if physics prop, else use yaw */
        float rot[12];
        int use_ode = (pr->type == PROP_PHYSICS && pr->physics_body_id >= 0 &&
                       cvar_get("phy_old") < 1.0f);
        if (use_ode) {
            physics_body_get_rotation(pr->physics_body_id, rot);
        } else {
            float yaw = pr->angles.x * 3.14159f / 180.0f;
            float cy = cosf(yaw), sy = sinf(yaw);
            /* Build 3x4 rotation matrix (row-major, ODE format) */
            rot[0]=cy;  rot[1]=-sy; rot[2]=0;  rot[3]=0;
            rot[4]=sy;  rot[5]=cy;  rot[6]=0;  rot[7]=0;
            rot[8]=0;   rot[9]=0;   rot[10]=1; rot[11]=0;
        }

        for (int i = 0; i < m->index_count; i++) {
            unsigned int vi = m->indices[i];
            if ((int)vi >= m->vertex_count) continue;

            MeshVertex* mv = &m->vertices[vi];
            float px = mv->pos[0] * sc;
            float py = mv->pos[1] * sc;
            float pz = mv->pos[2] * sc;

            /* Apply 3x3 rotation + translate */
            float rx = rot[0]*px + rot[1]*py + rot[2]*pz + pr->origin.x;
            float ry = rot[4]*px + rot[5]*py + rot[6]*pz + pr->origin.y;
            float rz = rot[8]*px + rot[9]*py + rot[10]*pz + pr->origin.z;

            /* Rotate normals too */
            float nx = rot[0]*mv->normal[0] + rot[1]*mv->normal[1] + rot[2]*mv->normal[2];
            float ny = rot[4]*mv->normal[0] + rot[5]*mv->normal[1] + rot[6]*mv->normal[2];
            float nz = rot[8]*mv->normal[0] + rot[9]*mv->normal[1] + rot[10]*mv->normal[2];

            verts[*count] = (BrushVertex){
                {rx, ry, rz},
                {mv->uv[0], mv->uv[1]},
                fidx,
                1.0f,
                {nx, ny, nz}
            };
            (*count)++;
        }
    }
}

static void build_all_brushes(BrushVertex* verts, int* count, int pass) {
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    for (int i = 0; i < bcount; i++) {
        if (!brushes[i] || brushes[i]->entity_owned) continue;
        build_one_brush(verts, count, brushes[i], VEC3_ZERO, pass);
    }
    int ecount;
    Entity** ents = entity_get_all(&ecount);
    for (int i = 0; i < ecount; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(e);
        if (!bd) continue;
        Vec3 offset = vec3_sub(e->origin, bd->spawn_origin);
        for (int j = 0; j < bd->brush_count; j++) {
            if (bd->brushes[j])
                build_one_brush(verts, count, bd->brushes[j], offset, pass);
        }
    }
}

static void render_update_brushes_silent(void) {
    BrushVertex* bv = malloc(MAX_BRUSH_VERTS * sizeof(BrushVertex));
    int bc = 0;

    /* Opaque brushes */
    build_all_brushes(bv, &bc, 0);
    /* Props (always opaque) */
    build_prop_vertices(bv, &bc);
    g_opaque_vert_count = (uint32_t)bc;

    /* Transparent brushes */
    build_all_brushes(bv, &bc, 1);
    g_transparent_vert_count = (uint32_t)bc - g_opaque_vert_count;

    g_brush_vert_count = (uint32_t)bc;
    void* data;
    vkMapMemory(g_device, g_brush_vb_mem, 0, bc * sizeof(BrushVertex), 0, &data);
    memcpy(data, bv, bc * sizeof(BrushVertex));
    vkUnmapMemory(g_device, g_brush_vb_mem);
    free(bv);
}

void render_draw_frame(float time) {
    (void)time;

    /* Lazy-load skybox if pending */
    render_load_skybox_internal();

    /* Re-upload brush geometry every frame for moving brush entities */
    render_update_brushes_silent();

    int f = g_current_frame;
    vkWaitForFences(g_device,1,&g_in_flight[f],VK_TRUE,UINT64_MAX);
    vkResetFences(g_device,1,&g_in_flight[f]);

    uint32_t img;
    vkAcquireNextImageKHR(g_device,g_swapchain,UINT64_MAX,g_image_available[f],VK_NULL_HANDLE,&img);

    /* MVP from player camera (or identity if no player yet) */
    float view[16], proj[16], vp[16];
    float aspect = (float)g_swapchain_extent.width / (float)g_swapchain_extent.height;
    Camera* cam = g_camera_override ? g_camera_override :
                  (player_get() ? player_get_camera(player_get()) : NULL);
    if (cam) {
        camera_view_matrix(cam, view);
        camera_proj_matrix(cam, aspect, proj);
        mat4_multiply(proj, view, vp);
    } else {
        /* Identity matrix — nothing to render in menu */
        memset(vp, 0, sizeof(vp));
        vp[0]=1; vp[5]=1; vp[10]=1; vp[15]=1;
    }

    VkCommandBuffer cmd = g_command_buffers[f];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(cmd, &begin);

    VkClearValue clears[2];
    clears[0].color = (VkClearColorValue){{0.05f,0.05f,0.08f,1.0f}};
    clears[1].depthStencil = (VkClearDepthStencilValue){1.0f, 0};
    VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass=g_render_pass,.framebuffer=g_framebuffers[img],
        .renderArea.extent=g_swapchain_extent,.clearValueCount=2,.pClearValues=clears};
    vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    /* Draw skybox first (behind everything) */
    if (g_sky_loaded && cam && g_sky_pipeline) {
        Vec3 fwd = camera_forward(cam);
        Vec3 rt = camera_right(cam);
        Vec3 up = VEC3(
            rt.y * fwd.z - rt.z * fwd.y,
            rt.z * fwd.x - rt.x * fwd.z,
            rt.x * fwd.y - rt.y * fwd.x
        );
        float fov_rad = cam->fov * 3.14159f / 180.0f;
        float fov_tan = tanf(fov_rad / 2.0f);
        float aspect_val = (float)g_swapchain_extent.width / (float)g_swapchain_extent.height;

        float sky_pc[16] = {
            fwd.x, fwd.y, fwd.z, 0,
            rt.x,  rt.y,  rt.z,  0,
            up.x,  up.y,  up.z,  0,
            fov_tan, aspect_val, 0, 0
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_sky_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_sky_layout, 0, 1, &g_sky_desc_set, 0, NULL);
        vkCmdPushConstants(cmd, g_sky_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, sky_pc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    /* Draw opaque brushes (with depth write) */
    if (g_opaque_vert_count > 0 && !g_show_wireframe) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush_layout, 0, 1, &g_desc_set, 0, NULL);
        vkCmdPushConstants(cmd, g_brush_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, vp);
        VkDeviceSize off=0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &g_brush_vb, &off);
        vkCmdDraw(cmd, g_opaque_vert_count, 1, 0, 0);
    }

    /* Draw transparent brushes (no depth write, so you can see through them) */
    if (g_transparent_vert_count > 0 && !g_show_wireframe) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush_transparent_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_brush_layout, 0, 1, &g_desc_set, 0, NULL);
        vkCmdPushConstants(cmd, g_brush_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, vp);
        VkDeviceSize off=0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &g_brush_vb, &off);
        vkCmdDraw(cmd, g_transparent_vert_count, 1, g_opaque_vert_count, 0);
    }

    /* Draw wireframe overlay */
    if (g_show_wireframe && g_wire_vert_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_wire_pipeline);
        vkCmdPushConstants(cmd, g_wire_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, vp);
        VkDeviceSize off=0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &g_wire_vb, &off);
        vkCmdDraw(cmd, g_wire_vert_count, 1, 0, 0);
    }

    /* Draw overlay */
    if (g_draw_overlay) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_overlay_pipeline);
        vkCmdPushConstants(cmd, g_overlay_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, g_overlay_color);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }

    /* Draw screen text */
    if (g_screen_text_count > 0 && g_text_vert_count > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_text_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_text_layout, 0, 1, &g_text_desc_set, 0, NULL);
        VkDeviceSize off=0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &g_text_vb, &off);
        vkCmdDraw(cmd, g_text_vert_count, 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount=1,.pWaitSemaphores=&g_image_available[f],.pWaitDstStageMask=&wait,
        .commandBufferCount=1,.pCommandBuffers=&cmd,
        .signalSemaphoreCount=1,.pSignalSemaphores=&g_render_finished[f]};
    vkQueueSubmit(g_graphics_queue,1,&si,g_in_flight[f]);

    VkPresentInfoKHR pi={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount=1,.pWaitSemaphores=&g_render_finished[f],
        .swapchainCount=1,.pSwapchains=&g_swapchain,.pImageIndices=&img};
    vkQueuePresentKHR(g_present_queue,&pi);

    g_current_frame = (f+1) % MAX_FRAMES_IN_FLIGHT;
}


int render_should_close(void) { return glfwWindowShouldClose(g_window); }
void render_poll_events(void) { glfwPollEvents(); }

GLFWwindow* render_get_window(void) { return g_window; }

void render_reset_mouse(double x, double y) {
    g_last_mouse_x = x;
    g_last_mouse_y = y;
}

void render_set_overlay(int enabled, float r, float g, float b, float a) {
    g_draw_overlay = enabled;
    g_overlay_color[0] = r;
    g_overlay_color[1] = g;
    g_overlay_color[2] = b;
    g_overlay_color[3] = a;
}

void render_clear_text(void) {
    g_screen_text_count = 0;
    g_text_vert_count = 0;
}

static void rebuild_text_verts(void) {
    TextVertex verts[MAX_TEXT_CHARS * 6];
    int total = 0;
    for (int i = 0; i < g_screen_text_count; i++) {
        total += font_build_text(verts + total, g_screen_texts[i],
                                 g_screen_text_x[i], g_screen_text_y[i],
                                 g_screen_text_scale[i],
                                 g_screen_text_color[i][0],
                                 g_screen_text_color[i][1],
                                 g_screen_text_color[i][2]);
    }
    g_text_vert_count = (uint32_t)total;
    if (total > 0) {
        void* data;
        vkMapMemory(g_device, g_text_vb_mem, 0, total * sizeof(TextVertex), 0, &data);
        memcpy(data, verts, total * sizeof(TextVertex));
        vkUnmapMemory(g_device, g_text_vb_mem);
    }
}

void render_add_text(const char* text) {
    render_add_text_at(text, 0.5f - (strlen(text) * 0.02f * 0.5f), 0.4f, 0.02f);
}

void render_add_text_at(const char* text, float x, float y, float scale) {
    render_add_text_colored(text, x, y, scale, 1, 1, 1);
}

void render_add_text_colored(const char* text, float x, float y, float scale,
                              float r, float g, float b) {
    if (g_screen_text_count >= 64) return;
    int i = g_screen_text_count++;
    strncpy(g_screen_texts[i], text, 127);
    g_screen_text_x[i] = x;
    g_screen_text_y[i] = y;
    g_screen_text_scale[i] = scale;
    g_screen_text_color[i][0] = r;
    g_screen_text_color[i][1] = g;
    g_screen_text_color[i][2] = b;
    rebuild_text_verts();
}

void render_get_mouse_screen(float* x, float* y) {
    double mx, my;
    glfwGetCursorPos(g_window, &mx, &my);
    int w, h;
    glfwGetWindowSize(g_window, &w, &h);
    *x = (float)(mx / w);
    *y = (float)(my / h);
}

void render_set_camera_override(Camera* cam) {
    g_camera_override = cam;
}

void render_set_skybox(const char* texture_path) {
    strncpy(g_sky_pending_path, texture_path, sizeof(g_sky_pending_path) - 1);
}

static void render_load_skybox_internal(void) {
    if (g_sky_loaded || g_sky_pending_path[0] == '\0') return;
    if (!g_device) return; /* renderer not ready */
    const char* texture_path = g_sky_pending_path;

    /* Load image with stb_image (already available from texture.c) */
    int w, h, ch;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* pixels = stbi_load(texture_path, &w, &h, &ch, 4);
    if (!pixels) {
        printf("[render] can't load skybox: %s\n", texture_path);
        g_sky_pending_path[0] = '\0';
        return;
    }

    VkDeviceSize size = (VkDeviceSize)(w * h * 4);

    /* Staging buffer */
    VkBuffer staging; VkDeviceMemory staging_mem;
    VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    vkCreateBuffer(g_device,&bci,NULL,&staging);
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(g_device,staging,&req);
    VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,
        .memoryTypeIndex=find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
    vkAllocateMemory(g_device,&mai,NULL,&staging_mem);
    vkBindBufferMemory(g_device,staging,staging_mem,0);
    void* data; vkMapMemory(g_device,staging_mem,0,size,0,&data);
    memcpy(data,pixels,size);
    vkUnmapMemory(g_device,staging_mem);
    stbi_image_free(pixels);
    stbi_set_flip_vertically_on_load(1); /* restore for other textures */

    /* Create image */
    VkImageCreateInfo ici={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=VK_FORMAT_R8G8B8A8_SRGB,.extent={.width=(uint32_t)w,.height=(uint32_t)h,.depth=1},
        .mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT};
    vkCreateImage(g_device,&ici,NULL,&g_sky_image);
    VkMemoryRequirements ireq; vkGetImageMemoryRequirements(g_device,g_sky_image,&ireq);
    VkMemoryAllocateInfo imai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=ireq.size,
        .memoryTypeIndex=find_memory_type(ireq.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    vkAllocateMemory(g_device,&imai,NULL,&g_sky_memory);
    vkBindImageMemory(g_device,g_sky_image,g_sky_memory,0);

    VkCommandBuffer cmd = begin_single_cmd();
    VkImageMemoryBarrier bar={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=g_sky_image,.subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1},
        .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&bar);
    VkBufferImageCopy region={.imageSubresource={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.layerCount=1},
        .imageExtent={.width=(uint32_t)w,.height=(uint32_t)h,.depth=1}};
    vkCmdCopyBufferToImage(cmd,staging,g_sky_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
    bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&bar);
    end_single_cmd(cmd);
    vkDestroyBuffer(g_device,staging,NULL); vkFreeMemory(g_device,staging_mem,NULL);

    /* Image view + sampler */
    VkImageViewCreateInfo vci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=g_sky_image,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_SRGB,
        .subresourceRange={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.levelCount=1,.layerCount=1}};
    vkCreateImageView(g_device,&vci,NULL,&g_sky_view);

    VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    vkCreateSampler(g_device,&sci,NULL,&g_sky_sampler);

    /* Descriptor set */
    VkDescriptorSetLayoutBinding bind={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dlci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&bind};
    vkCreateDescriptorSetLayout(g_device,&dlci,NULL,&g_sky_desc_layout);

    VkDescriptorPoolSize ps={.type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1};
    VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    vkCreateDescriptorPool(g_device,&dpci,NULL,&g_sky_desc_pool);

    VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=g_sky_desc_pool,.descriptorSetCount=1,.pSetLayouts=&g_sky_desc_layout};
    vkAllocateDescriptorSets(g_device,&dsai,&g_sky_desc_set);

    VkDescriptorImageInfo dii={.sampler=g_sky_sampler,.imageView=g_sky_view,.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wds={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=g_sky_desc_set,
        .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&dii};
    vkUpdateDescriptorSets(g_device,1,&wds,0,NULL);

    /* Pipeline */
    size_t vs,fs;
    uint32_t* vc=load_spirv("engine/shaders/skybox_vert.spv",&vs);
    uint32_t* fc=load_spirv("engine/shaders/skybox_frag.spv",&fs);
    VkShaderModule vm=create_shader_module(vc,vs), fm=create_shader_module(fc,fs);
    free(vc);free(fc);

    VkPipelineShaderStageCreateInfo stages[]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vm,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fm,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp={.width=(float)g_swapchain_extent.width,.height=(float)g_swapchain_extent.height,.maxDepth=1.0f};
    VkRect2D sc={.extent=g_swapchain_extent};
    VkPipelineViewportStateCreateInfo vps={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&vp,.scissorCount=1,.pScissors=&sc};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.lineWidth=1.0f,.cullMode=VK_CULL_MODE_NONE};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&ba};
    /* Depth: test but no write, always pass (renders behind everything) */
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_FALSE,.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL};

    VkPushConstantRange pcr={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,.size=64};
    VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1,.pSetLayouts=&g_sky_desc_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&pcr};
    vkCreatePipelineLayout(g_device,&pli,NULL,&g_sky_layout);

    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vps,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&cb,
        .pDepthStencilState=&ds,.layout=g_sky_layout,.renderPass=g_render_pass};
    vkCreateGraphicsPipelines(g_device,VK_NULL_HANDLE,1,&pi,NULL,&g_sky_pipeline);
    vkDestroyShaderModule(g_device,vm,NULL);
    vkDestroyShaderModule(g_device,fm,NULL);

    g_sky_loaded = 1;
    printf("[render] skybox loaded: %s (%dx%d)\n", texture_path, w, h);
}

void render_shutdown(void) {
    vkDeviceWaitIdle(g_device);

    vkDestroyBuffer(g_device,g_brush_vb,NULL); vkFreeMemory(g_device,g_brush_vb_mem,NULL);
    vkDestroyBuffer(g_device,g_wire_vb,NULL); vkFreeMemory(g_device,g_wire_vb_mem,NULL);

    vkDestroyImageView(g_device,g_depth_view,NULL);
    vkDestroyImage(g_device,g_depth_image,NULL);
    vkFreeMemory(g_device,g_depth_memory,NULL);

    for(int i=0;i<g_tex_count;i++){
        vkDestroyImageView(g_device,g_tex_views[i],NULL);
        vkDestroyImage(g_device,g_tex_images[i],NULL);
        vkFreeMemory(g_device,g_tex_memories[i],NULL);}
    free(g_tex_images); free(g_tex_memories); free(g_tex_views);
    vkDestroySampler(g_device,g_sampler,NULL);

    vkDestroyDescriptorPool(g_device,g_desc_pool,NULL);
    vkDestroyDescriptorSetLayout(g_device,g_desc_layout,NULL);

    for(int i=0;i<MAX_FRAMES_IN_FLIGHT;i++){
        vkDestroySemaphore(g_device,g_image_available[i],NULL);
        vkDestroySemaphore(g_device,g_render_finished[i],NULL);
        vkDestroyFence(g_device,g_in_flight[i],NULL);}
    vkDestroyCommandPool(g_device,g_command_pool,NULL);
    for(uint32_t i=0;i<g_image_count;i++) vkDestroyFramebuffer(g_device,g_framebuffers[i],NULL);
    free(g_framebuffers);
    vkDestroyPipeline(g_device,g_brush_pipeline,NULL);
    vkDestroyPipeline(g_device,g_brush_transparent_pipeline,NULL);
    vkDestroyPipelineLayout(g_device,g_brush_layout,NULL);
    vkDestroyPipeline(g_device,g_wire_pipeline,NULL);
    vkDestroyPipelineLayout(g_device,g_wire_layout,NULL);
    vkDestroyPipeline(g_device,g_overlay_pipeline,NULL);
    vkDestroyPipelineLayout(g_device,g_overlay_layout,NULL);
    vkDestroyRenderPass(g_device,g_render_pass,NULL);
    for(uint32_t i=0;i<g_image_count;i++) vkDestroyImageView(g_device,g_swapchain_views[i],NULL);
    free(g_swapchain_views); free(g_swapchain_images);
    vkDestroySwapchainKHR(g_device,g_swapchain,NULL);
    vkDestroyDevice(g_device,NULL);
    vkDestroySurfaceKHR(g_instance,g_surface,NULL);
    vkDestroyInstance(g_instance,NULL);
    glfwDestroyWindow(g_window); glfwTerminate();
    printf("[render] shutdown\n");
}
