// input.c - TI-99/4A Desktop Environment Input Handling
#include "vdp.h"
#include "kscan.h"
#include "config.h"
#include "types.h"

// Color globals from main.c
extern unsigned int g_color_bg;       // Background color
extern unsigned int g_color_fg;       // Foreground (text) color
extern unsigned int g_color_desktop;  // Desktop area color
extern unsigned int g_color_icon;     // Icon color
extern unsigned int g_color_select;   // Selection highlight color
extern unsigned int g_color_title_bg; // Title bar accent color
extern unsigned int g_color_title_fg; // Title bar text color
extern unsigned int g_color_divider;  // Divider line color

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
extern unsigned int window_enter_subdir(Window *win);
extern void window_show_path(Window *win);

// Forward declarations from device.c
extern unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page);
extern void cart_launch_rom(unsigned int entry_addr);
extern void cart_launch_grom(unsigned int entry_addr, unsigned int port);

// Forward declaration for EA5 loader (scratchloaderDesktop.asm)
extern void ea5ld(char *filename);

// Forward declaration for text file viewer
extern void viewer_view_file(const char *path, unsigned int is_variable, unsigned int rec_len);

// Forward declarations for bitmap viewer
extern unsigned int viewer_is_bitmap(const char *path);
extern void viewer_show_bitmap(const char *path);

// Forward declaration for local function
static void input_update_focus_status(void);

// restart attempt if EA5 fails from main.c
extern unsigned int restart_app;

// Restore selection display after restart (called from main.c)
void input_restore_selection(void);  // forward declaration

// Currently selected device on desktop (-1 = none selected)
static int g_selected = -1;
static int g_has_selection = 0;

// Key repeat state
static unsigned int g_last_key = 0;
static unsigned int g_repeat_count = 0;

// Key repeat timing (in frames at 60Hz)
#define KEY_REPEAT_INITIAL  20   // ~333ms initial delay
#define KEY_REPEAT_FAST     4    // ~67ms repeat rate

// Restore selection display after restart
// Called from main.c when VDP has been restored
void input_restore_selection(void) {
    extern void window_redraw_all(void);

    // Redraw selection reticule if there was one
    if (g_has_selection && g_selected >= 0 && (unsigned int)g_selected < g_app.device_count) {
        ui_select_device(g_selected, 1);
    }

    // Redraw any active windows
    window_redraw_all();
}

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

            if (file->type == FILE_TYPE_DIR) {
                // Enter subdirectory
                if (window_enter_subdir(win)) {
                    extern void window_redraw_all(void);
                    window_redraw_all();
                    input_update_focus_status();  // Show new path
                }
            } else if (file->type == FILE_TYPE_PROGRAM) {
                // Load and run PROGRAM file from disk
                // Build full path: "DEVICE.PATH.FILENAME"
                char fullpath[160];
                unsigned int pos = 0;
                unsigned int i;
                Device *dev = win->device;

                // Copy device name
                if (dev) {
                    for (i = 0; dev->name[i] && pos < 8; i++) {
                        fullpath[pos++] = dev->name[i];
                    }
                }
                fullpath[pos++] = '.';

                // Copy path (already has trailing dots between subdirs)
                for (i = 0; win->path[i] && pos < 140; i++) {
                    fullpath[pos++] = win->path[i];
                }

                // Copy filename
                for (i = 0; file->name[i] && pos < 158; i++) {
                    fullpath[pos++] = file->name[i];
                }
                fullpath[pos] = 0;

                // Check if this is a bitmap image (filename ends with _P)
                if (viewer_is_bitmap(fullpath)) {
                    viewer_show_bitmap(fullpath);
                    // viewer_show_bitmap sets restart_app
                    return;
                }

                ui_status("Loading...");
                // This does not return on success
                ea5ld(fullpath);
                // If we get here, load failed - but VDP is trashed, so we need to restart
                restart_app = 1;
                return;
            } else if (file->type == FILE_TYPE_ROM) {
                // Launch ROM program - branch to entry address
                // This does not return
                cart_launch_rom(file->size);
            } else if (file->type == FILE_TYPE_GROM) {
                // Launch GROM program via GPL interpreter
                // entry_addr in size, port in rec_len
                // This does not return
                cart_launch_grom(file->size, file->rec_len);
            } else if (file->type == FILE_TYPE_DISFIX || file->type == FILE_TYPE_DISVAR) {
                // Display file - open in viewer
                // Build full path: "DEVICE.PATH.FILENAME"
                char fullpath[160];
                unsigned int pos = 0;
                unsigned int i;
                Device *dev = win->device;

                // Copy device name
                if (dev) {
                    for (i = 0; dev->name[i] && pos < 8; i++) {
                        fullpath[pos++] = dev->name[i];
                    }
                }
                fullpath[pos++] = '.';

                // Copy path
                for (i = 0; win->path[i] && pos < 140; i++) {
                    fullpath[pos++] = win->path[i];
                }

                // Copy filename
                for (i = 0; file->name[i] && pos < 158; i++) {
                    fullpath[pos++] = file->name[i];
                }
                fullpath[pos] = 0;

                // Open viewer
                viewer_view_file(fullpath,
                                 (file->type == FILE_TYPE_DISVAR) ? 1 : 0,
                                 file->rec_len);

                // Redraw after viewer closes
                input_update_focus_status();
            } else {
                // Other file types (INT/FIX, INT/VAR) - not viewable as text
                ui_status("Cannot view this file type");
            }
        }
        return;
    }

    // On desktop with selection - open device window
    if (g_has_selection && g_selected >= 0 && (unsigned int)g_selected < g_app.device_count) {
        win_idx = window_open(&g_app.devices[g_selected]);
        if (win_idx >= 0) {
            // window_open already loaded directory and drew content
            // Update focus status to show path
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

// Color names for display (max 10 chars each)
static const char *color_names[] = {
    "Transpar  ", "Black     ", "Med Green ", "Lt Green  ",
    "Dk Blue   ", "Lt Blue   ", "Dk Red    ", "Cyan      ",
    "Med Red   ", "Lt Red    ", "Dk Yellow ", "Lt Yellow ",
    "Dk Green  ", "Magenta   ", "Gray      ", "White     "
};

// Menu window dimensions (centered: (32-22)/2 = 5)
#define MENU_X      5
#define MENU_Y      3
#define MENU_W      22
#define MENU_H      10

// Draw a simple menu window frame
static void menu_draw_frame(const char *title) {
    unsigned int i, j;

    // Top border
    vdpscreenchar(VDP_SCREEN_POS(MENU_Y, MENU_X), CHAR_WIN_TL);
    hchar(MENU_Y, MENU_X + 1, CHAR_WIN_H, MENU_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(MENU_Y, MENU_X + MENU_W - 1), CHAR_WIN_TR);

    // Title
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(MENU_Y, MENU_X + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        for (j = 0; title[j] && j < MENU_W - 4; j++) {
            VDPWD(title[j]);
        }
    }

    // Side borders and clear interior
    for (i = 1; i < MENU_H - 1; i++) {
        vdpscreenchar(VDP_SCREEN_POS(MENU_Y + i, MENU_X), CHAR_WIN_V);
        hchar(MENU_Y + i, MENU_X + 1, ' ', MENU_W - 2);
        vdpscreenchar(VDP_SCREEN_POS(MENU_Y + i, MENU_X + MENU_W - 1), CHAR_WIN_V);
    }

    // Bottom border
    vdpscreenchar(VDP_SCREEN_POS(MENU_Y + MENU_H - 1, MENU_X), CHAR_WIN_BL);
    hchar(MENU_Y + MENU_H - 1, MENU_X + 1, CHAR_WIN_H, MENU_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(MENU_Y + MENU_H - 1, MENU_X + MENU_W - 1), CHAR_WIN_BR);
}

// Draw a color line in the menu (total 20 chars: 1+1+8+10)
static void menu_draw_color_line(unsigned int row, unsigned int key, const char *label, unsigned int color) {
    unsigned int addr = gImage + VDP_SCREEN_POS(row, MENU_X + 1);
    unsigned int i;

    VDP_SET_ADDRESS_WRITE(addr);
    VDPWD(key);
    VDPWD(':');
    // Label (8 chars padded)
    for (i = 0; i < 8; i++) {
        VDPWD(label[i] ? label[i] : ' ');
    }
    // Color name (10 chars)
    for (i = 0; i < 10; i++) {
        VDPWD(color_names[color & 0x0F][i]);
    }
}

// Redraw colors in VDP after a color change
static void menu_apply_colors(void) {

    // Update VDP background register
    VDP_SET_REGISTER(VDP_REG_COL, g_color_bg);

    // Reload character colors
    extern void chars_init(void);
    chars_init();
}

// Helper to draw all color lines
static void menu_draw_all_colors(void) {
    menu_draw_color_line(MENU_Y + 1, '1', "Backgnd", g_color_bg);
    menu_draw_color_line(MENU_Y + 2, '2', "Text   ", g_color_fg);
    menu_draw_color_line(MENU_Y + 3, '3', "Desktop", g_color_desktop);
    menu_draw_color_line(MENU_Y + 4, '4', "Icon   ", g_color_icon);
    menu_draw_color_line(MENU_Y + 5, '5', "Select ", g_color_select);
    menu_draw_color_line(MENU_Y + 6, '6', "TitleBG", g_color_title_bg);
    menu_draw_color_line(MENU_Y + 7, '7', "TitleFG", g_color_title_fg);
    menu_draw_color_line(MENU_Y + 8, '8', "Divider", g_color_divider);
}

// Helper to refresh colors after change (VDP updates reflect immediately)
static void menu_refresh_colors(void) {
    menu_apply_colors();
    // Just redraw the color values in the menu - VDP color change is immediate
    menu_draw_all_colors();
}

// Color configuration menu
static void menu_color_config(void) {
    unsigned int key, lastkey = 0;
    extern void window_redraw_all(void);

    menu_draw_frame("Colors");
    menu_draw_all_colors();

    ui_status("1-7:Cycle  Fctn-9:Done");

    // Show preview selection brackets around icon 0
    if (g_app.device_count > 0) {
        ui_select_device(0, 1);
    }

    // Input loop
    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        key = KSCAN_KEY;

        if (key == KSCAN_NOKEY) {
            lastkey = 0;
            continue;
        }

        // Block autorepeat
        if (key == lastkey) continue;
        lastkey = key;

        // Small delay for debounce
        {
            volatile unsigned int d;
            for (d = 0; d < 500; d++);
        }

        if (key == KEY_BACK) {
            break;
        }

        // Cycle colors
        if (key == '1') {
            g_color_bg = (g_color_bg + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '2') {
            g_color_fg = (g_color_fg + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '3') {
            g_color_desktop = (g_color_desktop + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '4') {
            g_color_icon = (g_color_icon + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '5') {
            g_color_select = (g_color_select + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '6') {
            g_color_title_bg = (g_color_title_bg + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '7') {
            g_color_title_fg = (g_color_title_fg + 1) & 0x0F;
            menu_refresh_colors();
        }
        if (key == '8') {
            g_color_divider = (g_color_divider + 1) & 0x0F;
            menu_refresh_colors();
        }
    }

    // Remove preview selection brackets
    if (g_app.device_count > 0) {
        ui_select_device(0, 0);
    }

    // Redraw desktop and windows
    ui_draw_desktop();
    window_redraw_all();
    input_update_focus_status();
}

// Icon flags for each type
static const unsigned int icon_flags[] = {
    DEVICE_DISK, DEVICE_CART, DEVICE_HD, DEVICE_RAMDISK, 0, 0
};

// Icon characters for each type (TL, TR, BL, BR for each)
static const unsigned int icon_chars_tl[] = {
    CHAR_DISK_TL, CHAR_CART_TL, CHAR_HD_TL, CHAR_RAM_TL, CHAR_FILE_TL, CHAR_PROG_TL
};
static const unsigned int icon_chars_tr[] = {
    CHAR_DISK_TR, CHAR_CART_TR, CHAR_HD_TR, CHAR_RAM_TR, CHAR_FILE_TR, CHAR_PROG_TR
};
static const unsigned int icon_chars_bl[] = {
    CHAR_DISK_BL, CHAR_CART_BL, CHAR_HD_BL, CHAR_RAM_BL, CHAR_FILE_BL, CHAR_PROG_BL
};
static const unsigned int icon_chars_br[] = {
    CHAR_DISK_BR, CHAR_CART_BR, CHAR_HD_BR, CHAR_RAM_BR, CHAR_FILE_BR, CHAR_PROG_BR
};

// Icon menu dimensions (centered: (32-18)/2 = 7)
// Shows 6 icons in 2 rows of 3
#define ICON_MENU_X     7
#define ICON_MENU_Y     7
#define ICON_MENU_W     18
#define ICON_MENU_H     6

// Draw icon menu frame with icons
static void menu_draw_icon_frame(const char *title) {
    unsigned int i, j;
    unsigned int x = ICON_MENU_X;
    unsigned int y = ICON_MENU_Y;
    unsigned int w = ICON_MENU_W;
    unsigned int h = ICON_MENU_H;

    // Top border with title
    vdpscreenchar(VDP_SCREEN_POS(y, x), CHAR_WIN_TL);
    hchar(y, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(y, x + w - 1), CHAR_WIN_TR);
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(y, x + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        for (j = 0; title[j] && j < w - 4; j++) {
            VDPWD(title[j]);
        }
    }

    // Side borders and clear interior
    for (i = 1; i < h - 1; i++) {
        vdpscreenchar(VDP_SCREEN_POS(y + i, x), CHAR_WIN_V);
        hchar(y + i, x + 1, ' ', w - 2);
        vdpscreenchar(VDP_SCREEN_POS(y + i, x + w - 1), CHAR_WIN_V);
    }

    // Bottom border
    vdpscreenchar(VDP_SCREEN_POS(y + h - 1, x), CHAR_WIN_BL);
    hchar(y + h - 1, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(y + h - 1, x + w - 1), CHAR_WIN_BR);
}

// Draw the 6 icons in the icon menu (2 rows of 3)
static void menu_draw_icons(void) {
    unsigned int i, row, col;
    unsigned int base_x = ICON_MENU_X + 1;
    unsigned int base_y = ICON_MENU_Y + 1;

    // 3 icons per row, 5 chars each (num + : + TL + TR + space)
    for (i = 0; i < 6; i++) {
        row = i / 3;  // 0 or 1
        col = i % 3;  // 0, 1, or 2

        // Top row of icon: "N:XX"
        {
            unsigned int addr = gImage + VDP_SCREEN_POS(base_y + row * 2, base_x + col * 5);
            VDP_SET_ADDRESS_WRITE(addr);
            VDPWD('1' + i);
            VDPWD(':');
            VDPWD(icon_chars_tl[i]);
            VDPWD(icon_chars_tr[i]);
        }
        // Bottom row of icon: "  XX"
        {
            unsigned int addr = gImage + VDP_SCREEN_POS(base_y + row * 2 + 1, base_x + col * 5);
            VDP_SET_ADDRESS_WRITE(addr);
            VDPWD(' ');
            VDPWD(' ');
            VDPWD(icon_chars_bl[i]);
            VDPWD(icon_chars_br[i]);
        }
    }
}

// Icon selection submenu
static void menu_change_icon(unsigned int dev_idx) {
    unsigned int key, lastkey = 0;
    extern void window_redraw_all(void);
    Device *dev = &g_app.devices[dev_idx];

    menu_draw_icon_frame("Select Icon");
    menu_draw_icons();

    ui_status("1-6:Select  Fctn-9:Cancel");

    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        key = KSCAN_KEY;

        if (key == KSCAN_NOKEY) {
            lastkey = 0;
            continue;
        }

        if (key == lastkey) continue;
        lastkey = key;

        {
            volatile unsigned int d;
            for (d = 0; d < 500; d++);
        }

        if (key == KEY_BACK) {
            break;
        }

        if (key >= '1' && key <= '6') {
            unsigned int choice = key - '1';
            // Update device flags and icon
            // Clear old type flags, set new ones
            dev->flags = (dev->flags & ~(DEVICE_DISK | DEVICE_CART | DEVICE_HD | DEVICE_RAMDISK)) | icon_flags[choice];
            dev->icon = icon_chars_tl[choice];
            break;
        }
    }

    // Redraw
    ui_draw_desktop();
    window_redraw_all();
    ui_select_device(dev_idx, 1);
    input_update_focus_status();
}

// Confirmation dialog
static unsigned int menu_confirm(const char *msg) {
    unsigned int key;
    unsigned int len = 0;
    unsigned int x, w;

    while (msg[len]) len++;
    w = len + 4;
    x = (32 - w) / 2;

    // Draw small confirm box
    vdpscreenchar(VDP_SCREEN_POS(10, x), CHAR_WIN_TL);
    hchar(10, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(10, x + w - 1), CHAR_WIN_TR);

    vdpscreenchar(VDP_SCREEN_POS(11, x), CHAR_WIN_V);
    hchar(11, x + 1, ' ', w - 2);
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(11, x + 2);
        unsigned int i;
        VDP_SET_ADDRESS_WRITE(addr);
        for (i = 0; i < len; i++) {
            VDPWD(msg[i]);
        }
    }
    vdpscreenchar(VDP_SCREEN_POS(11, x + w - 1), CHAR_WIN_V);

    vdpscreenchar(VDP_SCREEN_POS(12, x), CHAR_WIN_V);
    hchar(12, x + 1, ' ', w - 2);
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(12, x + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        VDPWD('Y'); VDPWD(':'); VDPWD('Y'); VDPWD('e'); VDPWD('s');
        VDPWD(' '); VDPWD(' ');
        VDPWD('N'); VDPWD(':'); VDPWD('N'); VDPWD('o');
    }
    vdpscreenchar(VDP_SCREEN_POS(12, x + w - 1), CHAR_WIN_V);

    vdpscreenchar(VDP_SCREEN_POS(13, x), CHAR_WIN_BL);
    hchar(13, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(13, x + w - 1), CHAR_WIN_BR);

    // Wait for Y or N
    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        key = KSCAN_KEY;

        if (key == 'Y' || key == 'y') return 1;
        if (key == 'N' || key == 'n' || key == KEY_BACK) return 0;
    }
}

// Remove a device from the list
static void menu_remove_device(unsigned int dev_idx) {
    unsigned int i;
    extern void window_redraw_all(void);

    // Shift all devices after this one down
    for (i = dev_idx; i < g_app.device_count - 1; i++) {
        g_app.devices[i] = g_app.devices[i + 1];
    }
    g_app.device_count--;

    // Clear the last slot
    g_app.devices[g_app.device_count].cru_base = 0;
    g_app.devices[g_app.device_count].name[0] = 0;
    g_app.devices[g_app.device_count].icon = 0;
    g_app.devices[g_app.device_count].flags = DEVICE_NONE;

    // Clear selection
    g_has_selection = 0;
    g_selected = -1;

    // Redraw
    ui_draw_desktop();
    window_redraw_all();
    input_update_focus_status();
}

// Device popup menu dimensions (centered, smaller than icon menu)
#define DEV_MENU_X      10
#define DEV_MENU_Y      9
#define DEV_MENU_W      12
#define DEV_MENU_H      5

// Draw device popup frame
static void menu_draw_dev_frame(const char *title) {
    unsigned int i, j;
    unsigned int x = DEV_MENU_X;
    unsigned int y = DEV_MENU_Y;
    unsigned int w = DEV_MENU_W;
    unsigned int h = DEV_MENU_H;

    vdpscreenchar(VDP_SCREEN_POS(y, x), CHAR_WIN_TL);
    hchar(y, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(y, x + w - 1), CHAR_WIN_TR);
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(y, x + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        for (j = 0; title[j] && j < w - 4; j++) {
            VDPWD(title[j]);
        }
    }

    for (i = 1; i < h - 1; i++) {
        vdpscreenchar(VDP_SCREEN_POS(y + i, x), CHAR_WIN_V);
        hchar(y + i, x + 1, ' ', w - 2);
        vdpscreenchar(VDP_SCREEN_POS(y + i, x + w - 1), CHAR_WIN_V);
    }

    vdpscreenchar(VDP_SCREEN_POS(y + h - 1, x), CHAR_WIN_BL);
    hchar(y + h - 1, x + 1, CHAR_WIN_H, w - 2);
    vdpscreenchar(VDP_SCREEN_POS(y + h - 1, x + w - 1), CHAR_WIN_BR);
}

// Device popup menu (when icon is selected)
static void menu_device_popup(unsigned int dev_idx) {
    unsigned int key, lastkey = 0;
    extern void window_redraw_all(void);
    Device *dev = &g_app.devices[dev_idx];
    // Cartridge is always device 0 and cannot be removed (even if icon changed)
    unsigned int is_cart = (dev_idx == 0);

    menu_draw_dev_frame("Device");

    // Draw options
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(DEV_MENU_Y + 1, DEV_MENU_X + 1);
        VDP_SET_ADDRESS_WRITE(addr);
        VDPWD('I'); VDPWD(':'); VDPWD('C'); VDPWD('h'); VDPWD('g');
        VDPWD(' '); VDPWD('I'); VDPWD('c'); VDPWD('o'); VDPWD('n');
    }

    if (!is_cart) {
        unsigned int addr = gImage + VDP_SCREEN_POS(DEV_MENU_Y + 2, DEV_MENU_X + 1);
        VDP_SET_ADDRESS_WRITE(addr);
        VDPWD('R'); VDPWD(':'); VDPWD('R'); VDPWD('e'); VDPWD('m');
        VDPWD('o'); VDPWD('v'); VDPWD('e');
    }

    ui_status("I:Icon  R:Remove  F9:Cancel");

    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        key = KSCAN_KEY;

        if (key == KSCAN_NOKEY) {
            lastkey = 0;
            continue;
        }

        if (key == lastkey) continue;
        lastkey = key;

        {
            volatile unsigned int d;
            for (d = 0; d < 500; d++);
        }

        if (key == KEY_BACK) {
            break;
        }

        if (key == 'I' || key == 'i') {
            menu_change_icon(dev_idx);
            return;  // Already redrawn
        }

        if ((key == 'R' || key == 'r') && !is_cart) {
            if (menu_confirm("Remove device?")) {
                menu_remove_device(dev_idx);
                return;  // Already redrawn
            }
            // Redraw menu if cancelled
            ui_draw_desktop();
            window_redraw_all();
            ui_select_device(dev_idx, 1);
            menu_draw_dev_frame("Device");
            {
                unsigned int addr = gImage + VDP_SCREEN_POS(DEV_MENU_Y + 1, DEV_MENU_X + 1);
                VDP_SET_ADDRESS_WRITE(addr);
                VDPWD('I'); VDPWD(':'); VDPWD('C'); VDPWD('h'); VDPWD('g');
                VDPWD(' '); VDPWD('I'); VDPWD('c'); VDPWD('o'); VDPWD('n');
            }
            {
                unsigned int addr = gImage + VDP_SCREEN_POS(DEV_MENU_Y + 2, DEV_MENU_X + 1);
                VDP_SET_ADDRESS_WRITE(addr);
                VDPWD('R'); VDPWD(':'); VDPWD('R'); VDPWD('e'); VDPWD('m');
                VDPWD('o'); VDPWD('v'); VDPWD('e');
            }
            ui_status("I:Icon  R:Remove  F9:Cancel");
        }
    }

    // Redraw on cancel
    ui_draw_desktop();
    window_redraw_all();
    ui_select_device(dev_idx, 1);
    input_update_focus_status();
}

// Handle Menu
static void input_menu(void) {
    if (g_has_selection && g_selected >= 0 && (unsigned int)g_selected < g_app.device_count) {
        // Device selected - show device popup
        menu_device_popup(g_selected);
    } else {
        // No selection - show color config
        menu_color_config();
    }
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
        // Update status to show new focus
        input_update_focus_status();
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
        // Update status to show new focus
        input_update_focus_status();
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

// Update status line to show current focus/path
static void input_update_focus_status(void) {
    Window *win;

    if (g_app.focus == FOCUS_DESKTOP) {
        ui_status("Desktop");
    } else {
        win = window_get_focused();
        window_show_path(win);
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
