/*
 * console.h — Simple cvar/command system (Source-style)
 *
 * CVars: named variables (like sv_gravity, host_timescale)
 * Commands: named functions you can call (like noclip, kill)
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#define MAX_CVARS    64
#define MAX_COMMANDS 64
#define CVAR_NAME_LEN 64

/* CVar */
typedef struct {
    char    name[CVAR_NAME_LEN];
    float   value;
    float   default_value;
    char    string_value[128];
} CVar;

/* Command callback */
typedef void (*CmdFunc)(const char* args);

typedef struct {
    char    name[CVAR_NAME_LEN];
    CmdFunc func;
} ConsoleCmd;

/* CVars */
CVar*   cvar_register(const char* name, float default_val);
CVar*   cvar_find(const char* name);
float   cvar_get(const char* name);
void    cvar_set(const char* name, float value);

/* Commands */
void    cmd_register(const char* name, CmdFunc func);
void    cmd_execute(const char* input);  /* parse and run "command args" */

void console_init(void);
void console_shutdown(void);

#endif /* CONSOLE_H */
