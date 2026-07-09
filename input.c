// input.c - TI-99/4A Desktop Environment Input Handling
#include "kscan.h"
#include "config.h"
#include "types.h"

// Forward declarations from ui.c
extern void ui_select_device(unsigned int idx, unsigned int selected);
extern void ui_clear_selection(void);
extern void ui_status(const char *msg);
extern void ui_draw_desktop(void);

// Forward declarations from window.c
extern int window_open(Device *dev);
extern void window_close(unsigned int win_idx);
extern void window_toggle_focus(void);
extern Window *window_get_focused(void);
extern void window_scroll_left(Window *win);
extern void window_scroll_right(Window *win);
extern void window_cursor_up(Window *win);
extern void window_cursor_down(Window *win);
extern void window_page_up(Window *win);
extern void window_page_down(Window *win);
extern void window_get_title(Window *win, char *buf);
extern void window_load_dir(Window *win, unsigned int page);
extern void window_draw_indicator(void);
extern void window_show(Window *win);

// Forward declarations from device.c
extern unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page);
extern void cart_launch_rom(unsigned int entry_addr);
extern void cart_launch_grom(unsigned int entry_addr, unsigned int port);

// Forward declaration for local function
static void input_update_focus_status(void);

// Currently selected device on desktop (-1 = none selected)
static int g_selected = -1;
static int g_has_selection = 0;

// Key repeat state
static unsigned int g_last_key = 0;
static unsigned int g_repeat_count = 0;

// Key repeat timing (in frames at 60Hz)
#define KEY_REPEAT_INITIAL  20   // ~333ms initial delay
#define KEY_REPEAT_FAST     4    // ~67ms repeat rate

// Handle device selection (1-9 keys)
static void input_select_device(int num) {
    if (num >= 0 && (unsigned int)num < g_app.device_count) {
        // Deselect old if any
        if (g_has_selection) {
            ui_select_device(g_selected, 0);
        }
        // Select new
        g_selected = num;
        g_has_selection = 1;
        ui_select_device(g_selected, 1);
    }
}

// Handle navigation keys
static void input_nav_up(void) {
    Window *win;

    // If window focused, move cursor up in file list
    if (g_app.focus != FOCUS_DESKTOP) {
        win = window_get_focused();
        if (win) {
            window_cursor_up(win);
        }
        return;
    }

    // Desktop navigation - select first device if none selected
    if (!g_has_selection) {
        if (g_app.device_count > 0) {
            input_select_device(0);
        }
        return;
    }
    // Column-major: up moves by 1 within column
    if (g_selected > 0 && (g_selected % ICON_ROWS) > 0) {
        ui_select_device(g_selected, 0);
        g_selected--;
        ui_select_device(g_selected, 1);
        window_draw_indicator();
    }
}

static void input_nav_down(void) {
    Window *win;

    // If window focused, move cursor down in file list
    if (g_app.focus != FOCUS_DESKTOP) {
        win = window_get_focused();
        if (win) {
            window_cursor_down(win);
        }
        return;
    }

    // Desktop navigation - select first device if none selected
    if (!g_has_selection) {
        if (g_app.device_count > 0) {
            input_select_device(0);
        }
        return;
    }
    // Column-major: down moves by 1 within column
    if (g_selected + 1 < (int)g_app.device_count && ((g_selected + 1) % ICON_ROWS) != 0) {
        ui_select_device(g_selected, 0);
        g_selected++;
        ui_select_device(g_selected, 1);
        window_draw_indicator();
    }
}

static void input_nav_left(void) {
    Window *win;

    // If window focused, scroll content left
    if (g_app.focus != FOCUS_DESKTOP) {
        win = window_get_focused();
        if (win) {
            window_scroll_left(win);
        }
        return;
    }

    // Desktop navigation - select first device if none selected
    if (!g_has_selection) {
        if (g_app.device_count > 0) {
            input_select_device(0);
        }
        return;
    }
    // Column-major: left moves by ICON_ROWS to previous column
    if (g_selected >= ICON_ROWS) {
        ui_select_device(g_selected, 0);
        g_selected -= ICON_ROWS;
        ui_select_device(g_selected, 1);
        window_draw_indicator();
    }
}

static void input_nav_right(void) {
    Window *win;

    // If window focused, scroll content right
    if (g_app.focus != FOCUS_DESKTOP) {
        win = window_get_focused();
        if (win) {
            window_scroll_right(win);
        }
        return;
    }

    // Desktop navigation - select first device if none selected
    if (!g_has_selection) {
        if (g_app.device_count > 0) {
            input_select_device(0);
        }
        return;
    }
    // Column-major: right moves by ICON_ROWS to next column
    if (g_selected + ICON_ROWS < (int)g_app.device_count) {
        ui_select_device(g_selected, 0);
        g_selected += ICON_ROWS;
        ui_select_device(g_selected, 1);
        window_draw_indicator();
    }
}

// Handle Enter - open selected device
static void input_open(void) {
    Window *win;
    int win_idx;

    // If a window is focused, Enter acts on selected file
    if (g_app.focus != FOCUS_DESKTOP) {
        win = window_get_focused();
        if (win && win->file_count > 0 && win->cursor_y < win->file_count) {
            FileEntry *file = &win->files[win->cursor_y];

            if (file->type == FILE_TYPE_ROM) {
                // Launch ROM program - branch to entry address
                // This does not return
                cart_launch_rom(file->size);
            } else if (file->type == FILE_TYPE_GROM) {
                // Launch GROM program via GPL interpreter
                // entry_addr in size, port in rec_len
                // This does not return
                cart_launch_grom(file->size, file->rec_len);
            } else {
                // Disk file - TODO
                ui_status("File selected");
            }
        }
        return;
    }

    // On desktop with selection - open device window
    if (g_has_selection && g_selected >= 0 && (unsigned int)g_selected < g_app.device_count) {
        win_idx = window_open(&g_app.devices[g_selected]);
        if (win_idx >= 0) {
            // Load directory from device
            win = &g_app.windows[win_idx];
            window_load_dir(win, 0);
            // Redraw to show files
            extern void window_redraw_all(void);
            window_redraw_all();
            // Update focus status
            input_update_focus_status();
        }
    } else {
        ui_status("Select a device first");
    }
}

// Forward declaration from device.c
extern void device_scan(void);

// Handle Scan - enumerate devices via CRU
static void input_scan(void) {
    device_scan();
}

// Handle Menu
static void input_menu(void) {
    // TODO: Implement menu display
    // Menu should include option to enter device name manually
    // (for devices not found by scan or with custom names)
    ui_status("Menu not implemented");
}

// Handle Fctn-9 (back/close/deselect)
static void input_back(void) {
    unsigned int win_idx;
    extern void window_redraw_all(void);

    if (g_app.focus == FOCUS_WINDOW1) {
        // Close window 1
        win_idx = 0;
        window_close(win_idx);
        // Redraw desktop to clear window area
        ui_draw_desktop();
        // Redraw remaining window if any
        window_redraw_all();
        // Only show selection if focus went to desktop
        if (g_app.focus == FOCUS_DESKTOP && g_has_selection) {
            ui_select_device(g_selected, 1);
        }
    } else if (g_app.focus == FOCUS_WINDOW2) {
        // Close window 2
        win_idx = 1;
        window_close(win_idx);
        // Redraw desktop to clear window area
        ui_draw_desktop();
        // Redraw remaining window if any
        window_redraw_all();
        // Only show selection if focus went to desktop
        if (g_app.focus == FOCUS_DESKTOP && g_has_selection) {
            ui_select_device(g_selected, 1);
        }
    } else if (g_has_selection) {
        // On desktop with selection - deselect
        ui_clear_selection();
        g_has_selection = 0;
        g_selected = -1;
    }
}

// Handle page up in window
static void input_page_up(void) {
    Window *win = window_get_focused();
    if (win) {
        window_page_up(win);
    }
}

// Handle page down in window
static void input_page_down(void) {
    Window *win = window_get_focused();
    if (win) {
        window_page_down(win);
    }
}

// Update status line to show current focus
static void input_update_focus_status(void) {
    char buf[16];
    Window *win;

    if (g_app.focus == FOCUS_DESKTOP) {
        ui_status("Desktop");
    } else {
        win = window_get_focused();
        if (win) {
            // Format: "L-CART" or "R-DSK1" etc.
            if (g_app.focus == FOCUS_WINDOW1) {
                buf[0] = 'R'; buf[1] = '-';  // Right window
            } else {
                buf[0] = 'L'; buf[1] = '-';  // Left window
            }
            window_get_title(win, buf + 2);
            ui_status(buf);
        }
    }
}

// Handle focus toggle (KEY_AID)
static void input_toggle_focus(void) {
    window_toggle_focus();
    input_update_focus_status();
}

// Check if key should be processed (handles repeat throttling)
// Returns 1 if key should be processed, 0 if throttled
static unsigned int input_check_repeat(unsigned int key) {
    if (key != g_last_key) {
        // New key pressed
        g_last_key = key;
        g_repeat_count = 0;
        return 1;
    }

    // Same key held
    g_repeat_count++;

    // Initial delay
    if (g_repeat_count == KEY_REPEAT_INITIAL) {
        return 1;
    }

    // Fast repeat after initial delay
    if (g_repeat_count > KEY_REPEAT_INITIAL) {
        if ((g_repeat_count - KEY_REPEAT_INITIAL) % KEY_REPEAT_FAST == 0) {
            return 1;
        }
    }

    return 0;
}

// Process keyboard input - called each frame
// control keys verified in system and added to kscan.h
void input_process(void) {
    unsigned int key;

    // Scan keyboard in BASIC mode
    kscan(KSCAN_MODE_BASIC);
    key = KSCAN_KEY;

    // No key pressed?
    if (key == KSCAN_NOKEY) {
        g_last_key = 0;
        g_repeat_count = 0;
        return;
    }

    // Check key repeat throttling
    if (!input_check_repeat(key)) {
        return;
    }

    // Handle Fctn-9 (close/back) - no repeat
    if (key == KEY_BACK) {
        if (g_repeat_count == 0) {
            input_back();
        }
        return;
    }

    // Handle KEY_AID (Fctn-7) - toggle focus - no repeat
    if (key == KEY_AID) {
        if (g_repeat_count == 0) {
            input_toggle_focus();
        }
        return;
    }

    // Handle KEY_CLEAR (Fctn-4) - page down
    if (key == KEY_CLEAR) {
        input_page_down();
        return;
    }

    // Handle KEY_PROCEED (Fctn-6) - page up
    if (key == KEY_PROCEED) {
        input_page_up();
        return;
    }

    // Handle Fctn-E (up)
    if (key == KEY_UP) {
        input_nav_up();
        return;
    }

    // Handle Fctn-X (down)
    if (key == KEY_DOWN) {
        input_nav_down();
        return;
    }

    // Handle Fctn-S (left)
    if (key == KEY_LEFT) {
        input_nav_left();
        return;
    }

    // Handle Fctn-D (right)
    if (key == KEY_RIGHT) {
        input_nav_right();
        return;
    }

    // Device number keys 1-9
    // On desktop: select device
    // In window: reload window with new device
    if (key >= '1' && key <= '9') {
        unsigned int dev_idx = key - '1';
        if (dev_idx < g_app.device_count) {
            if (g_app.focus == FOCUS_DESKTOP) {
                input_select_device(dev_idx);
            } else {
                // Reload current window with new device
                Window *win = window_get_focused();
                if (win) {
                    Device *dev = &g_app.devices[dev_idx];
                    win->device = dev;
                    win->scroll_x = g_app.windows[0].scroll_x;  // Keep scroll position
                    win->scroll_y = 0;

                    // Load directory from device
                    window_load_dir(win, 0);

                    // Redraw window
                    extern void window_redraw_all(void);
                    window_redraw_all();

                    // Update status to show new device name
                    input_update_focus_status();
                }
            }
        }
        return;
    }

    // Enter - open - no repeat
    if (key == KEY_ENTER) {
        if (g_repeat_count == 0) {
            input_open();
        }
        return;
    }

    // M - menu (only on desktop)
    if (key == 'M' || key == 'm') {
        if (g_app.focus == FOCUS_DESKTOP) {
            input_menu();
        }
        return;
    }

    // S - scan (only on desktop)
    if (key == 'S' || key == 's') {
        if (g_app.focus == FOCUS_DESKTOP) {
            input_scan();
        }
        return;
    }
}
