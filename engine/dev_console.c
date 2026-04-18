/*
 * dev_console.c — In-game developer console
 */
#include "dev_console.h"
#include "console.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define MAX_INPUT    256
#define MAX_HISTORY  16
#define MAX_LINE     128

static int  g_open = 0;
static char g_input[MAX_INPUT];
static int  g_input_len = 0;
static char g_history[MAX_HISTORY][MAX_LINE];
static int  g_history_count = 0;

/* Command history (up/down arrow) */
static char g_cmd_history[MAX_HISTORY][MAX_INPUT];
static int  g_cmd_history_count = 0;
static int  g_cmd_history_pos = -1;

void dev_console_init(void) {
    g_open = 0;
    g_input_len = 0;
    g_input[0] = '\0';
    g_history_count = 0;
    g_cmd_history_count = 0;
    g_cmd_history_pos = -1;
}

int dev_console_is_open(void) {
    return g_open;
}

void dev_console_toggle(void) {
    g_open = !g_open;
    if (g_open) {
        g_input_len = 0;
        g_input[0] = '\0';
        g_cmd_history_pos = -1;
    }
}

void dev_console_print(const char* text) {
    /* Shift history up if full */
    if (g_history_count >= MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++)
            strcpy(g_history[i], g_history[i + 1]);
        g_history_count = MAX_HISTORY - 1;
    }
    strncpy(g_history[g_history_count++], text, MAX_LINE - 1);
    g_history[g_history_count - 1][MAX_LINE - 1] = '\0';
}

static void execute_input(void) {
    if (g_input_len == 0) return;

    /* Add to output */
    char line[MAX_LINE];
    snprintf(line, sizeof(line), "] %s", g_input);
    dev_console_print(line);

    /* Add to command history */
    if (g_cmd_history_count < MAX_HISTORY) {
        strcpy(g_cmd_history[g_cmd_history_count++], g_input);
    } else {
        for (int i = 0; i < MAX_HISTORY - 1; i++)
            strcpy(g_cmd_history[i], g_cmd_history[i + 1]);
        strcpy(g_cmd_history[MAX_HISTORY - 1], g_input);
    }

    /* Capture printf output — we can't easily, so just execute */
    cmd_execute(g_input);

    /* Clear input */
    g_input_len = 0;
    g_input[0] = '\0';
    g_cmd_history_pos = -1;
}

void dev_console_char(unsigned int codepoint) {
    if (!g_open) return;
    if (codepoint == '`' || codepoint == '~') return; /* ignore tilde */
    if (codepoint < 32 || codepoint > 126) return;
    if (g_input_len >= MAX_INPUT - 1) return;

    g_input[g_input_len++] = (char)codepoint;
    g_input[g_input_len] = '\0';
}

void dev_console_key(int key, int action) {
    if (!g_open) return;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_BACKSPACE && g_input_len > 0) {
        g_input[--g_input_len] = '\0';
    }
    if (key == GLFW_KEY_ENTER) {
        execute_input();
    }
    if (key == GLFW_KEY_UP) {
        if (g_cmd_history_count > 0) {
            if (g_cmd_history_pos < 0) g_cmd_history_pos = g_cmd_history_count - 1;
            else if (g_cmd_history_pos > 0) g_cmd_history_pos--;
            strcpy(g_input, g_cmd_history[g_cmd_history_pos]);
            g_input_len = (int)strlen(g_input);
        }
    }
    if (key == GLFW_KEY_DOWN) {
        if (g_cmd_history_pos >= 0) {
            g_cmd_history_pos++;
            if (g_cmd_history_pos >= g_cmd_history_count) {
                g_cmd_history_pos = -1;
                g_input_len = 0;
                g_input[0] = '\0';
            } else {
                strcpy(g_input, g_cmd_history[g_cmd_history_pos]);
                g_input_len = (int)strlen(g_input);
            }
        }
    }
}

void dev_console_render(void) {
    if (!g_open) return;

    /* Dark background for console area (top half of screen) */
    render_set_overlay(1, 0, 0, 0, 0.75f);

    /* Render history lines */
    float y = 0.02f;
    float scale = 0.011f;
    int start = g_history_count > 12 ? g_history_count - 12 : 0;
    for (int i = start; i < g_history_count; i++) {
        render_add_text_colored(g_history[i], 0.01f, y, scale, 0.8f, 0.8f, 0.8f);
        y += 0.035f;
    }

    /* Render input line */
    char prompt[MAX_INPUT + 4];
    snprintf(prompt, sizeof(prompt), "> %s_", g_input);
    render_add_text_colored(prompt, 0.01f, 0.45f, 0.013f, 0, 1, 0);
}
