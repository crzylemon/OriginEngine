/*
 * render.h — Vulkan renderer
 */
#ifndef RENDER_H
#define RENDER_H

#include "camera.h"

int     render_init(int width, int height, const char* title);
void    render_update_brushes(void);   /* upload brush geometry to GPU */
void    render_process_input(float dt); /* handle WASD + mouse */
void    render_draw_frame(float time);
int     render_should_close(void);
void    render_poll_events(void);
void    render_shutdown(void);
Camera* render_get_camera(void);

/* Get the GLFW window (for state management in main) */
struct GLFWwindow* render_get_window(void);

/* Reset mouse tracking position (call after re-capturing mouse) */
void    render_reset_mouse(double x, double y);

/* Screen overlay (for pause/menu darkening) */
void    render_set_overlay(int enabled, float r, float g, float b, float a);

/* Screen text (rendered on top of everything) */
void    render_clear_text(void);
void    render_add_text(const char* text);
void    render_add_text_at(const char* text, float x, float y, float scale);
void    render_add_text_colored(const char* text, float x, float y, float scale,
                                 float r, float g, float b);

/* Get mouse position in 0..1 screen coords */
void    render_get_mouse_screen(float* x, float* y);

/* Override the camera used for rendering (NULL = use player camera) */
void    render_set_camera_override(Camera* cam);

/* Skybox: load a panoramic texture */
void    render_set_skybox(const char* texture_path);

#endif /* RENDER_H */
