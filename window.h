// window.h - TI-99/4A Desktop Environment Window Management (window.c)
#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"

// Default horizontal scroll position for new windows (persisted in prefs)
extern unsigned int g_default_scroll_x;

void window_init(void);
int window_open(Device *dev);
void window_close(unsigned int win_idx);
void window_toggle_focus(void);
Window *window_get_focused(void);
void window_draw_indicator(void);
void window_hide(Window *win);
void window_show(Window *win);
void window_get_title(Window *win, char *buf);
void window_scroll_left(Window *win);
void window_scroll_right(Window *win);
void window_cursor_up(Window *win);
void window_cursor_down(Window *win);
void window_load_dir(Window *win, unsigned int page);
unsigned int window_enter_subdir(Window *win);
unsigned int window_up_dir(Window *win);
void window_show_path(Window *win);
void window_page_up(Window *win);
void window_page_down(Window *win);
void window_redraw_all(void);
void window_add_test_file(Window *win, const char *name, unsigned int type, unsigned int size, unsigned int rec_len);

#endif // WINDOW_H
