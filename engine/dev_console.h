/*
 * dev_console.h — In-game developer console (~ to toggle)
 *
 * Renders a text input + output history overlay.
 * Uses the existing console system for command execution.
 */
#ifndef DEV_CONSOLE_H
#define DEV_CONSOLE_H

/* Init/shutdown */
void dev_console_init(void);

/* Returns 1 if console is open (game should pause input) */
int  dev_console_is_open(void);

/* Toggle open/closed */
void dev_console_toggle(void);

/* Feed a character (from GLFW char callback) */
void dev_console_char(unsigned int codepoint);

/* Feed a key press (backspace, enter, etc.) */
void dev_console_key(int key, int action);

/* Render the console overlay (call every frame) */
void dev_console_render(void);

/* Add a line to the output history */
void dev_console_print(const char* text);

#endif /* DEV_CONSOLE_H */
