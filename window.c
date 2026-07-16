// window.c - TI-99/4A Desktop Environment Window Management
#include "vdp.h"
#include "config.h"
#include "types.h"
#include "string.h"

// External UI functions
extern void ui_status(const char *msg);

// Forward declarations
static void window_draw_content(Window *win);
void window_show(Window *win);

// Forward declarations from device.c
extern unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page);
extern unsigned int device_read_dir_with_path(Device *dev, const char *path, char *volume_name, FileEntry *files, unsigned int max_files, unsigned int page);

// from ui.c
extern void ui_draw_window(unsigned int x, unsigned int y, unsigned int w, unsigned int h, const char *title);

// Global default scroll position (remembered across windows, persisted in prefs)
unsigned int g_default_scroll_x = FILE_COL_NAME;

// Type prefix strings (2 chars each) - used with record length
// PROG, GROM, ROM, DIR have no record length, others are XX + nnn format
static const char *type_prefix[] = {
    "PROG",   // FILE_TYPE_PROGRAM (special case - no rec len)
    "IF",     // FILE_TYPE_INTFIX
    "IV",     // FILE_TYPE_INTVAR
    "DF",     // FILE_TYPE_DISFIX
    "DV",     // FILE_TYPE_DISVAR
    "GROM",   // FILE_TYPE_GROM (special case - no rec len)
    "ROM ",   // FILE_TYPE_ROM (special case - no rec len)
    "DIR "    // FILE_TYPE_DIR (special case - no rec len)
};

// Initialize window system
void window_init(void) {
    unsigned int i;

    g_app.win_count = 0;
    g_app.focus = FOCUS_DESKTOP;
    g_default_scroll_x = FILE_COL_NAME;  // Start at filename column

    memset(g_app.windows, 0, sizeof(g_app.windows));
    for (i = 0; i < MAX_WINDOWS; i++) {
        g_app.windows[i].scroll_x = FILE_COL_NAME;
    }

    // Set window positions
    // Window 0 = right half
    g_app.windows[0].x = WIN1_X;
    g_app.windows[0].y = WIN1_Y;
    g_app.windows[0].w = WIN_WIDTH;
    g_app.windows[0].h = WIN_HEIGHT;

    // Window 1 = left half
    g_app.windows[1].x = WIN2_X;
    g_app.windows[1].y = WIN2_Y;
    g_app.windows[1].w = WIN_WIDTH;
    g_app.windows[1].h = WIN_HEIGHT;
}

// Format a 16-bit address as 4 hex digits
// Writes 4 chars to buf
static void format_hex(unsigned int addr, char *buf) {
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = hex[(addr >> 12) & 0xF];
    buf[1] = hex[(addr >> 8) & 0xF];
    buf[2] = hex[(addr >> 4) & 0xF];
    buf[3] = hex[addr & 0xF];
}

// Format a file size with suffix (K, M, or raw if <1000)
// Writes 4 chars to buf
static void format_size(unsigned int size, char *buf) {
    if (size >= 1000) {
        // Show as K (sectors are ~256 bytes, so K is reasonable)
        unsigned int k = size / 4;  // Approximate KB
        if (k >= 1000) {
            // Show as M
            buf[0] = '0' + (k / 1000) % 10;
            buf[1] = '0' + (k / 100) % 10;
            buf[2] = '0' + (k / 10) % 10;
            buf[3] = 'M';
        } else if (k >= 100) {
            buf[0] = '0' + (k / 100) % 10;
            buf[1] = '0' + (k / 10) % 10;
            buf[2] = '0' + k % 10;
            buf[3] = 'K';
        } else if (k >= 10) {
            buf[0] = ' ';
            buf[1] = '0' + (k / 10) % 10;
            buf[2] = '0' + k % 10;
            buf[3] = 'K';
        } else {
            buf[0] = ' ';
            buf[1] = ' ';
            buf[2] = '0' + k % 10;
            buf[3] = 'K';
        }
    } else if (size >= 100) {
        buf[0] = '0' + (size / 100) % 10;
        buf[1] = '0' + (size / 10) % 10;
        buf[2] = '0' + size % 10;
        buf[3] = ' ';
    } else if (size >= 10) {
        buf[0] = ' ';
        buf[1] = '0' + (size / 10) % 10;
        buf[2] = '0' + size % 10;
        buf[3] = ' ';
    } else {
        buf[0] = ' ';
        buf[1] = ' ';
        buf[2] = '0' + size % 10;
        buf[3] = ' ';
    }
}

// Build a formatted line for a file entry
// Format: "999K PROG  FILENAME..." or "6025 GROM  FILENAME..." (FILE_LINE_LEN chars)
static void format_file_line(FileEntry *file, char *line) {
    unsigned int i;
    const char *prefix;
    unsigned int rec_len;

    // Size/Address (4 chars) - hex for GROM/ROM, size for disk files
    if (file->type == FILE_TYPE_GROM || file->type == FILE_TYPE_ROM) {
        format_hex(file->size, line);  // Entry point address in hex
    } else {
        format_size(file->size, line);
    }

    // Space
    line[4] = ' ';

    // Type (5 chars): "PROG ", "GROM ", "ROM  ", "DIR  " or "XXnnn" where XX is type prefix and nnn is rec_len
    prefix = (file->type <= FILE_TYPE_DIR) ? type_prefix[file->type] : "??";

    if (file->type == FILE_TYPE_PROGRAM) {
        // Program files: "PROG "
        memcpy(&line[5], "PROG ", 5);
    } else if (file->type == FILE_TYPE_GROM) {
        // GROM cartridge: "GROM "
        memcpy(&line[5], "GROM ", 5);
    } else if (file->type == FILE_TYPE_ROM) {
        // ROM cartridge: "ROM  "
        memcpy(&line[5], "ROM  ", 5);
    } else if (file->type == FILE_TYPE_DIR) {
        // Subdirectory: "DIR  "
        memcpy(&line[5], "DIR  ", 5);
    } else {
        // Data files: 2-char prefix + 3-digit record length
        line[5] = prefix[0];
        line[6] = prefix[1];
        rec_len = file->rec_len;
        if (rec_len >= 100) {
            line[7] = '0' + (rec_len / 100) % 10;
            line[8] = '0' + (rec_len / 10) % 10;
            line[9] = '0' + rec_len % 10;
        } else if (rec_len >= 10) {
            line[7] = ' ';
            line[8] = '0' + (rec_len / 10) % 10;
            line[9] = '0' + rec_len % 10;
        } else {
            line[7] = ' ';
            line[8] = ' ';
            line[9] = '0' + rec_len % 10;
        }
    }

    // Space
    line[10] = ' ';

    // Filename (up to 32 chars, pad with spaces) - starts at position 11
    for (i = 0; i < FILE_NAME_LEN; i++) {
        if (file->name[i]) {
            line[11 + i] = file->name[i];
        } else {
            // Pad rest with spaces
            while (i < FILE_NAME_LEN) {
                line[11 + i] = ' ';
                i++;
            }
            break;
        }
    }

    // Null terminate (though we use fixed length)
    line[FILE_LINE_LEN] = 0;
}

// Draw window content (file list)
static void window_draw_content(Window *win) {
    unsigned int i, row;
    unsigned int x, y;
    unsigned int visible_rows;
    unsigned int addr;
    FileEntry *file;
    char line[FILE_LINE_LEN + 1];

    x = win->x + 1;  // Inside left border
    y = win->y + 1;  // Below title bar
    visible_rows = win->h - 2;  // Minus top and bottom borders

    for (i = 0; i < visible_rows && i < win->file_count; i++) {
        row = y + i;
        file = &win->files[i];

        // Build formatted line
        format_file_line(file, line);

        // Clear the row first
        hchar(row, x, ' ', win->w - 2);

        // Draw selection indicator if this is selected row
        if (i == win->cursor_y) {
            vdpscreenchar(VDP_SCREEN_POS(row, x), '>');
        }

        // Draw file info (start at column 2 for selector space)
        addr = gImage + VDP_SCREEN_POS(row, x + 1);
        VDP_SET_ADDRESS_WRITE(addr);

        // Output visible portion based on scroll_x
        {
            unsigned int j;
            unsigned int start = win->scroll_x;
            unsigned int max_chars = win->w - 4;  // Leave room for selector and borders

            for (j = 0; j < max_chars; j++) {
                unsigned int idx = start + j;
                if (idx < FILE_LINE_LEN) {
                    VDPWD(line[idx]);
                } else {
                    VDPWD(' ');
                }
            }
        }
    }

    // Clear remaining rows if fewer files than visible rows
    for (; i < visible_rows; i++) {
        hchar(y + i, x, ' ', win->w - 2);
    }
}

// Set window to expanded (single window) position
static void window_set_expanded(Window *win) {
    win->x = WIN_EXPANDED_X;
    win->w = WIN_EXPANDED_W;
}

// Set window to half-screen position
static void window_set_half(unsigned int win_idx) {
    Window *win = &g_app.windows[win_idx];
    if (win_idx == 0) {
        win->x = WIN1_X;  // Right half
    } else {
        win->x = WIN2_X;  // Left half
    }
    win->w = WIN_WIDTH;
}

// Open a window for a device
// Returns window index (0 or 1) or -1 if failed
int window_open(Device *dev) {
    Window *win;
    unsigned int win_idx;
    char title[12];

    // Find first available window slot
    // First window goes to right (index 0), second to left (index 1)
    if (!g_app.windows[0].active) {
        win_idx = 0;
    } else if (!g_app.windows[1].active) {
        win_idx = 1;
    } else {
        // Both windows full
        ui_status("No window slots");
        return -1;
    }

    win = &g_app.windows[win_idx];

    // Initialize window state
    win->active = 1;
    win->device = dev;
    win->scroll_x = g_default_scroll_x;  // Use global default
    win->scroll_y = 0;
    win->cursor_y = 0;
    win->file_count = 0;
    win->page_start = 0;
    win->volume_name[0] = 0;
    win->path[0] = 0;  // Start at device root

    // Set window size based on whether this is the only window
    if (g_app.win_count == 0) {
        // First window - expanded mode
        window_set_expanded(win);
    } else {
        // Second window opening - shrink the other window to half
        unsigned int other_idx = (win_idx == 0) ? 1 : 0;
        window_set_half(other_idx);
        window_set_half(win_idx);

        // Redraw desktop to clear area, then redraw the other window
        {
            extern void ui_draw_desktop(void);
            ui_draw_desktop();
            window_show(&g_app.windows[other_idx]);
        }
    }

    g_app.win_count++;

    // Load directory first (this also reads the volume name into the window)
    {
        unsigned int visible_rows = win->h - 2;
        win->file_count = device_read_dir_with_path(dev, win->path, win->volume_name, win->files, visible_rows, 0);
    }

    // Build window title - use volume name if available, else device name
    {
        unsigned int i;
        const char *src;

        // Use volume name if it's not blank
        if (win->volume_name[0] != 0 && win->volume_name[0] != ' ') {
            src = win->volume_name;
        } else {
            src = dev->name;
        }

        for (i = 0; i < 10 && src[i] && src[i] != ' '; i++) {
            title[i] = src[i];
        }
        title[i] = 0;
    }

    // Draw window frame
    ui_draw_window(win->x, win->y, win->w, win->h, title);

    // Draw content (already loaded)
    window_draw_content(win);

    // Focus the new window
    g_app.focus = (win_idx == 0) ? FOCUS_WINDOW1 : FOCUS_WINDOW2;

    return win_idx;
}

// Close a window
void window_close(unsigned int win_idx) {
    Window *win;
    unsigned int other_idx;

    if (win_idx >= MAX_WINDOWS) return;

    win = &g_app.windows[win_idx];

    if (!win->active) return;

    win->active = 0;
    win->device = 0;
    win->file_count = 0;

    if (g_app.win_count > 0) {
        g_app.win_count--;
    }

    // If there's still one window, expand it
    other_idx = (win_idx == 0) ? 1 : 0;
    if (g_app.windows[other_idx].active) {
        window_set_expanded(&g_app.windows[other_idx]);
    }

    // If this window was focused, move focus
    if ((win_idx == 0 && g_app.focus == FOCUS_WINDOW1) ||
        (win_idx == 1 && g_app.focus == FOCUS_WINDOW2)) {
        // Try to focus other window, else desktop
        if (win_idx == 0 && g_app.windows[1].active) {
            g_app.focus = FOCUS_WINDOW2;
        } else if (win_idx == 1 && g_app.windows[0].active) {
            g_app.focus = FOCUS_WINDOW1;
        } else {
            g_app.focus = FOCUS_DESKTOP;
        }
    }
}

// Forward declarations for hide/show
void window_hide(Window *win);
void window_show(Window *win);

// Toggle focus between windows and desktop
void window_toggle_focus(void) {
    // When both windows open: cycle Window1 <-> Window2 only (skip desktop)
    // When one window open: cycle Desktop <-> Window (hide/show window)
    // When no windows: stay on desktop

    unsigned int old_focus = g_app.focus;

    if (g_app.win_count == 2) {
        // Both windows open - toggle between them, skip desktop
        if (g_app.focus == FOCUS_WINDOW1) {
            g_app.focus = FOCUS_WINDOW2;
        } else {
            g_app.focus = FOCUS_WINDOW1;
        }
        return;
    }

    // One or no windows - include desktop in cycle
    switch (g_app.focus) {
        case FOCUS_DESKTOP:
            if (g_app.windows[0].active) {
                g_app.focus = FOCUS_WINDOW1;
                window_show(&g_app.windows[0]);
            } else if (g_app.windows[1].active) {
                g_app.focus = FOCUS_WINDOW2;
                window_show(&g_app.windows[1]);
            }
            // else stay on desktop
            break;

        case FOCUS_WINDOW1:
            if (g_app.windows[1].active) {
                g_app.focus = FOCUS_WINDOW2;
            } else {
                g_app.focus = FOCUS_DESKTOP;
                window_hide(&g_app.windows[0]);
            }
            break;

        case FOCUS_WINDOW2:
            g_app.focus = FOCUS_DESKTOP;
            window_hide(&g_app.windows[1]);
            break;

        default:
            g_app.focus = FOCUS_DESKTOP;
            break;
    }
}

// Get currently focused window (NULL if desktop focused)
Window *window_get_focused(void) {
    if (g_app.focus == FOCUS_WINDOW1 && g_app.windows[0].active) {
        return &g_app.windows[0];
    }
    if (g_app.focus == FOCUS_WINDOW2 && g_app.windows[1].active) {
        return &g_app.windows[1];
    }
    return 0;
}

// Draw window indicator strip on right edge of screen
// Shows that a window is open but hidden
void window_draw_indicator(void) {
    unsigned int i;
    unsigned int row;

    // Only draw if one window is open and focus is on desktop
    if (g_app.win_count != 1 || g_app.focus != FOCUS_DESKTOP) {
        return;
    }

    // Draw vertical line on right edge (column 31)
    for (i = 0; i < WIN_HEIGHT; i++) {
        row = WIN1_Y + i;
        vdpscreenchar(VDP_SCREEN_POS(row, SCREEN_WIDTH - 1), CHAR_WIN_V);
    }
}

// Hide the window (erase content, show indicator)
void window_hide(Window *win) {
    unsigned int saved_active;

    if (!win || !win->active) return;

    // Temporarily mark window as inactive so ui_draw_desktop redraws full area
    saved_active = win->active;
    win->active = 0;

    // Redraw full desktop
    extern void ui_draw_desktop(void);
    ui_draw_desktop();

    // Restore active flag (window is still logically open, just hidden)
    win->active = saved_active;

    // Draw indicator strip on right edge
    window_draw_indicator();
}

// Show the window (redraw fully)
void window_show(Window *win) {
    char title[12];
    const char *src;
    unsigned int i;

    if (!win || !win->active) return;

    // Use volume name if available, else device name
    if (win->volume_name[0] != 0 && win->volume_name[0] != ' ') {
        src = win->volume_name;
    } else if (win->device) {
        src = win->device->name;
    } else {
        src = "?";
    }

    for (i = 0; i < 10 && src[i] && src[i] != ' '; i++) {
        title[i] = src[i];
    }
    title[i] = 0;

    ui_draw_window(win->x, win->y, win->w, win->h, title);
    window_draw_content(win);
}

// Get window title (device name)
// Writes to buf (must be at least 8 chars)
void window_get_title(Window *win, char *buf) {
    Device *dev;

    if (!win || !win->device) {
        buf[0] = '?';
        buf[1] = 0;
        return;
    }

    dev = win->device;

    // Copy device name from struct
    {
        unsigned int i;
        for (i = 0; i < 7 && dev->name[i]; i++) {
            buf[i] = dev->name[i];
        }
        buf[i] = 0;
    }
}

// Scroll window content left
void window_scroll_left(Window *win) {
    if (!win || !win->active) return;

    if (win->scroll_x > 0) {
        win->scroll_x--;
        g_default_scroll_x = win->scroll_x;  // Remember as default
        window_draw_content(win);
    }
}

// Scroll window content right
void window_scroll_right(Window *win) {
    if (!win || !win->active) return;

    // Max scroll is line length minus visible width
    if (win->scroll_x < FILE_LINE_LEN - (win->w - 4)) {
        win->scroll_x++;
        g_default_scroll_x = win->scroll_x;  // Remember as default
        window_draw_content(win);
    }
}

// Move cursor up in file list
void window_cursor_up(Window *win) {
    if (!win || !win->active) return;

    if (win->cursor_y > 0) {
        win->cursor_y--;
        window_draw_content(win);
    }
}

// Move cursor down in file list
void window_cursor_down(Window *win) {
    unsigned int visible_rows;

    if (!win || !win->active) return;

    visible_rows = win->h - 2;  // Minus borders

    // Limit to both file count and visible rows
    if (win->cursor_y + 1 < win->file_count && win->cursor_y + 1 < visible_rows) {
        win->cursor_y++;
        window_draw_content(win);
    }
}

// Load directory entries into window from device
void window_load_dir(Window *win, unsigned int page) {
    unsigned int visible_rows;

    if (!win || !win->active || !win->device) return;

    visible_rows = win->h - 2;  // Minus borders

    // Reset window state for new page
    win->cursor_y = 0;
    win->page_start = page;

    // Read directory from device with path (request only visible rows)
    // Pass NULL for volume_name since we already have it from initial open
    win->file_count = device_read_dir_with_path(win->device, win->path, 0, win->files, visible_rows, page);

    // Redraw content
    window_draw_content(win);
}

// Enter a subdirectory - appends dir name to path and reloads
// Returns 1 if entered, 0 if not a directory
unsigned int window_enter_subdir(Window *win) {
    FileEntry *file;
    unsigned int i;
    unsigned int path_len;

    if (!win || !win->active || win->cursor_y >= win->file_count) return 0;

    file = &win->files[win->cursor_y];

    // Only enter if it's a directory
    if (file->type != FILE_TYPE_DIR) return 0;

    // Find current path length
    for (path_len = 0; win->path[path_len]; path_len++);

    // Append directory name to path (with trailing dot)
    for (i = 0; file->name[i] && path_len < 126; i++) {
        win->path[path_len++] = file->name[i];
    }
    win->path[path_len++] = '.';
    win->path[path_len] = 0;

    // Reset to page 0 and reload
    win->page_start = 0;
    win->cursor_y = 0;
    window_load_dir(win, 0);

    return 1;
}

// Go up one directory level - strips the last path component
// win->path holds everything after the device name ("SUB1.SUB2." form),
// so an empty path means we are at the device root
// Returns 1 if it went up, 0 if already at root
unsigned int window_up_dir(Window *win) {
    unsigned int len;
    int i;

    if (!win || !win->active) return 0;

    for (len = 0; win->path[len]; len++);
    if (len == 0) return 0;

    // Path ends with a trailing dot - find the dot before the last
    // component and truncate just after it (or to empty at top level)
    for (i = (int)len - 2; i >= 0; i--) {
        if (win->path[i] == '.') break;
    }
    win->path[i + 1] = 0;

    // Reset to page 0 and reload
    win->page_start = 0;
    win->cursor_y = 0;
    window_load_dir(win, 0);

    return 1;
}

// Show the current path on the status bar
// Format: "DEV.PATH" or just "DEV." if at root
void window_show_path(Window *win) {
    char buf[32];
    unsigned int i, pos;
    Device *dev;

    if (!win || !win->active || !win->device) {
        ui_status("Desktop");
        return;
    }

    dev = win->device;
    pos = 0;

    // Copy device name
    for (i = 0; dev->name[i] && pos < 8; i++) {
        buf[pos++] = dev->name[i];
    }
    buf[pos++] = '.';

    // Append path (truncate if too long for status bar)
    for (i = 0; win->path[i] && pos < 30; i++) {
        buf[pos++] = win->path[i];
    }

    // If path was truncated, show ellipsis
    if (win->path[i]) {
        buf[pos - 1] = '.';
        buf[pos - 2] = '.';
    }

    buf[pos] = 0;
    ui_status(buf);
}

// Page up (previous page of files)
void window_page_up(Window *win) {
    if (!win || !win->active) return;

    if (win->page_start > 0) {
        window_load_dir(win, win->page_start - 1);
    }
}

// Page down (next page of files)
void window_page_down(Window *win) {
    unsigned int visible_rows;

    if (!win || !win->active) return;

    visible_rows = win->h - 2;  // Minus borders

    // Only page down if current page is full (more entries may exist)
    if (win->file_count >= visible_rows) {
        window_load_dir(win, win->page_start + 1);
    }
}

// Redraw all active windows
void window_redraw_all(void) {
    unsigned int i;
    char title[12];
    Window *win;
    const char *src;

    for (i = 0; i < MAX_WINDOWS; i++) {
        win = &g_app.windows[i];
        if (!win->active) continue;

        // Build title - use volume name if available, else device name
        if (win->volume_name[0] != 0 && win->volume_name[0] != ' ') {
            src = win->volume_name;
        } else if (win->device) {
            src = win->device->name;
        } else {
            src = "?";
        }

        {
            unsigned int j;
            for (j = 0; j < 10 && src[j] && src[j] != ' '; j++) {
                title[j] = src[j];
            }
            title[j] = 0;
        }

        ui_draw_window(win->x, win->y, win->w, win->h, title);
        window_draw_content(win);
    }
}

// Add a test file to a window (for testing)
void window_add_test_file(Window *win, const char *name, unsigned int type, unsigned int size, unsigned int rec_len) {
    FileEntry *file;
    unsigned int i;

    if (!win || win->file_count >= WIN_MAX_FILES) return;

    file = &win->files[win->file_count];

    // Copy name
    for (i = 0; i < 31 && name[i]; i++) {
        file->name[i] = name[i];
    }
    file->name[i] = 0;

    file->type = type;
    file->size = size;
    file->rec_len = rec_len;

    win->file_count++;
}
