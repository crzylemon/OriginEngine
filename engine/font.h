/*
 * font.h — Simple bitmap font rendering
 *
 * Built-in 8x8 pixel font. Renders text as textured quads
 * in screen space (0,0 = top-left, 1,1 = bottom-right).
 */
#ifndef FONT_H
#define FONT_H

/* Max characters per frame */
#define MAX_TEXT_CHARS 2048

typedef struct {
    float pos[2];  /* screen position */
    float uv[2];   /* font atlas UV */
    float col[3];  /* text color */
} TextVertex;

/* Get the built-in 8x8 font bitmap (128 chars, 8x8 each = 1024x8 atlas) */
void font_get_atlas(unsigned char** pixels, int* width, int* height);

/* Build text vertices. Returns vertex count (6 per char = 2 triangles).
   x,y in screen coords (0..1), scale in screen units. */
int font_build_text(TextVertex* verts, const char* text,
                    float x, float y, float scale,
                    float r, float g, float b);

int font_build_text_centered(TextVertex* verts, const char* text,
                             float center_x, float y, float scale,
                             float r, float g, float b);

/* Hit test: is mouse (in 0..1 screen coords) over this text? */
int font_text_hit_test(const char* text, float x, float y, float scale,
                       float mouse_x, float mouse_y);

#endif /* FONT_H */
