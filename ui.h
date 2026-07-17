// ui.h - TI-99/4A Desktop Environment Screen Drawing (ui.c)
#ifndef UI_H
#define UI_H

// Draw a window frame with title at the given position
void ui_draw_window(unsigned int x, unsigned int y, unsigned int w, unsigned int h, const char *title);

// Draw the full desktop: title bar, device icons, status line
void ui_draw_desktop(void);

// Clear all selection brackets from the desktop
void ui_clear_selection(void);

// Draw or clear selection brackets around a device icon
void ui_select_device(unsigned int idx, unsigned int selected);

// Show a message on the status line
void ui_status(const char *msg);

#endif // UI_H
