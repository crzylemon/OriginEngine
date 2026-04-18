/*
 * console.c — Console system implementation
 */
#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CVar       g_cvars[MAX_CVARS];
static int        g_cvar_count = 0;

static ConsoleCmd g_cmds[MAX_COMMANDS];
static int        g_cmd_count = 0;

void console_init(void) {
    g_cvar_count = 0;
    g_cmd_count = 0;
    printf("[console] initialized\n");
}

void console_shutdown(void) {
    printf("[console] shutdown\n");
}

/* ── CVars ───────────────────────────────────────────────────── */

CVar* cvar_register(const char* name, float default_val) {
    if (g_cvar_count >= MAX_CVARS) {
        printf("[console] ERROR: max cvars reached!\n");
        return NULL;
    }

    /* Check if already exists */
    CVar* existing = cvar_find(name);
    if (existing) return existing;

    CVar* cv = &g_cvars[g_cvar_count++];
    strncpy(cv->name, name, CVAR_NAME_LEN - 1);
    cv->value = default_val;
    cv->default_value = default_val;
    snprintf(cv->string_value, sizeof(cv->string_value), "%g", default_val);

    return cv;
}

CVar* cvar_find(const char* name) {
    for (int i = 0; i < g_cvar_count; i++) {
        if (strcmp(g_cvars[i].name, name) == 0)
            return &g_cvars[i];
    }
    return NULL;
}

float cvar_get(const char* name) {
    CVar* cv = cvar_find(name);
    return cv ? cv->value : 0.0f;
}

void cvar_set(const char* name, float value) {
    CVar* cv = cvar_find(name);
    if (cv) {
        cv->value = value;
        snprintf(cv->string_value, sizeof(cv->string_value), "%g", value);
        printf("[console] %s = %g\n", name, value);
    } else {
        printf("[console] unknown cvar: %s\n", name);
    }
}

/* ── Commands ────────────────────────────────────────────────── */

void cmd_register(const char* name, CmdFunc func) {
    if (g_cmd_count >= MAX_COMMANDS) {
        printf("[console] ERROR: max commands reached!\n");
        return;
    }

    ConsoleCmd* cmd = &g_cmds[g_cmd_count++];
    strncpy(cmd->name, name, CVAR_NAME_LEN - 1);
    cmd->func = func;
}

void cmd_execute(const char* input) {
    if (!input || !input[0]) return;

    /* Split into command + args */
    char buf[256];
    strncpy(buf, input, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* cmd_name = strtok(buf, " ");
    char* args = strtok(NULL, "");

    if (!cmd_name) return;

    /* Check commands first */
    for (int i = 0; i < g_cmd_count; i++) {
        if (strcmp(g_cmds[i].name, cmd_name) == 0) {
            g_cmds[i].func(args);
            return;
        }
    }

    /* Check if it's a cvar set: "name value" */
    CVar* cv = cvar_find(cmd_name);
    if (cv) {
        if (args) {
            cvar_set(cmd_name, (float)atof(args));
        } else {
            printf("[console] %s = %g (default: %g)\n",
                   cv->name, cv->value, cv->default_value);
        }
        return;
    }

    printf("[console] unknown command: %s\n", cmd_name);
}
