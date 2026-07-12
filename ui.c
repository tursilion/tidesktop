// ui.c - TI-99/4A Desktop Environment UI Rendering
#include "vdp.h"
#include "config.h"
#include "types.h"

// External globals
extern const char *g_title_string;
extern unsigned int g_color_bg;
extern unsigned int g_color_fg;

// Track current selection for clearing
static int g_sel_visible = 0;
static unsigned int g_sel_row = 0;
static unsigned int g_sel_col = 0;

// Draw a horizontal line of characters
static void ui_hline(unsigned int row, unsigned int col, unsigned int len, unsigned int ch) {
    hchar(row, col, ch, len);
}

// Draw a vertical line of characters
static void ui_vline(unsigned int row, unsigned int col, unsigned int len, unsigned int ch) {
    vchar(row, col, ch, len);
}

// Draw a single character at position
static void ui_putchar(unsigned int row, unsigned int col, unsigned int ch) {
    vdpscreenchar(VDP_SCREEN_POS(row, col), ch);
}

// Draw a string at position (up to maxlen chars)
static void ui_putstr(unsigned int row, unsigned int col, const char *str, unsigned int maxlen) {
    unsigned int i;
    unsigned int addr;

    addr = gImage + VDP_SCREEN_POS(row, col);
    VDP_SET_ADDRESS_WRITE(addr);

    for (i = 0; i < maxlen && str[i] != 0; i++) {
        VDPWD(str[i]);
    }
}

// Draw a 2x2 icon at position
static void ui_draw_icon_2x2(unsigned int row, unsigned int col,
                              unsigned int tl, unsigned int tr,
                              unsigned int bl, unsigned int br) {
    ui_putchar(row,     col,     tl);
    ui_putchar(row,     col + 1, tr);
    ui_putchar(row + 1, col,     bl);
    ui_putchar(row + 1, col + 1, br);
}

// Draw futuristic title bar with inset mini-font
static void ui_draw_title_bar(const char *title) {
    unsigned int i;
    unsigned int title_start;
    unsigned int title_len;
    unsigned int addr;

    // Calculate title positioning (centered)
    for (title_len = 0; title[title_len] != 0 && title_len < 20; title_len++);
    title_start = (SCREEN_WIDTH - title_len) / 2;

    // Draw left cap
    ui_putchar(0, 0, CHAR_TITLE_L);

    // Draw fill up to title
    for (i = 1; i < title_start; i++) {
        ui_putchar(0, i, CHAR_TITLE_FILL);
    }

    // Draw title text using mini-font (inset appearance)
    addr = gImage + VDP_SCREEN_POS(0, title_start);
    VDP_SET_ADDRESS_WRITE(addr);
    for (i = 0; i < title_len && title[i] != 0; i++) {
        VDPWD(char_to_title(title[i]));
    }

    // Draw fill after title
    for (i = title_start + title_len; i < SCREEN_WIDTH - 1; i++) {
        ui_putchar(0, i, CHAR_TITLE_FILL);
    }

    // Draw right cap
    ui_putchar(0, SCREEN_WIDTH - 1, CHAR_TITLE_R);
}

// Draw a bordered window with graphical borders
void ui_draw_window(unsigned int x, unsigned int y, unsigned int w, unsigned int h, const char *title) {
    unsigned int i;

    // Top border
    ui_putchar(y, x, CHAR_WIN_TL);
    ui_hline(y, x + 1, w - 2, CHAR_WIN_H);
    ui_putchar(y, x + w - 1, CHAR_WIN_TR);

    // Side borders
    ui_vline(y + 1, x, h - 2, CHAR_WIN_V);
    ui_vline(y + 1, x + w - 1, h - 2, CHAR_WIN_V);

    // Bottom border
    ui_putchar(y + h - 1, x, CHAR_WIN_BL);
    ui_hline(y + h - 1, x + 1, w - 2, CHAR_WIN_H);
    ui_putchar(y + h - 1, x + w - 1, CHAR_WIN_BR);

    // Clear interior
    for (i = 1; i < h - 1; i++) {
        ui_hline(y + i, x + 1, w - 2, ' ');
    }

    // Draw title (centered)
    if (title) {
        ui_putstr(y, x + 2, title, w - 4);
    }
}

// Draw a device icon on the desktop (2x2 graphical icon)
static void ui_draw_device_icon(unsigned int idx, Device *dev) {
    unsigned int row, col;
    unsigned int tl, tr, bl, br;

    // Layout: 8 columns, 5 rows - fill vertically first (column-major)
    // Each icon slot is 4 cols wide, 4 rows tall (2 icon + 1 label + 1 space)
    row = ICON_START_ROW + (idx % ICON_ROWS) * ICON_ROW_HEIGHT;
    col = ICON_START_COL + (idx / ICON_ROWS) * ICON_COL_WIDTH + 1;  // +1 for bracket space

    // Use stored icon (TL character), derive others from it
    // Icons are stored as consecutive chars: TL, TR, BL, BR
    tl = dev->icon;
    tr = dev->icon + 1;
    bl = dev->icon + 2;
    br = dev->icon + 3;

    // Draw the 2x2 icon
    ui_draw_icon_2x2(row, col, tl, tr, bl, br);

    // Draw label below icon (device name, up to 4 chars centered)
    ui_putstr(row + 2, col - 1, dev->name, 4);
}

// Check if a column is covered by any active window
static unsigned int ui_col_covered(unsigned int col) {
    unsigned int i;
    Window *win;

    for (i = 0; i < MAX_WINDOWS; i++) {
        win = &g_app.windows[i];
        if (win->active) {
            if (col >= win->x && col < win->x + win->w) {
                return 1;
            }
        }
    }
    return 0;
}

// Draw the desktop background and icons
// Avoids drawing over active windows
void ui_draw_desktop(void) {
    unsigned int i;
    unsigned int row, col;
    unsigned int start_col, run_len;

    // Clear title bar area (row 0) - always visible
    ui_hline(0, 0, SCREEN_WIDTH, ' ');

    // Clear divider row 1 - always visible
    ui_hline(1, 0, SCREEN_WIDTH, ' ');

    // Clear content area (rows 2-21), avoiding windows
    // Find runs of uncovered columns to clear efficiently
    for (row = 2; row < SCREEN_HEIGHT - 2; row++) {
        col = 0;
        while (col < SCREEN_WIDTH) {
            // Skip covered columns
            while (col < SCREEN_WIDTH && ui_col_covered(col)) {
                col++;
            }
            if (col >= SCREEN_WIDTH) break;

            // Find run of uncovered columns
            start_col = col;
            while (col < SCREEN_WIDTH && !ui_col_covered(col)) {
                col++;
            }
            run_len = col - start_col;

            // Clear this run
            ui_hline(row, start_col, run_len, ' ');
        }
    }

    // Clear status area (rows 22-23) - always visible
    ui_hline(SCREEN_HEIGHT - 2, 0, SCREEN_WIDTH, ' ');
    ui_hline(SCREEN_HEIGHT - 1, 0, SCREEN_WIDTH, ' ');

    // Draw futuristic title bar at top (uses global title string)
    ui_draw_title_bar(g_title_string);

    // Draw divider line below title (row 1) - uses inverse green colors
    ui_hline(1, 0, SCREEN_WIDTH, CHAR_DIVIDER);

    // Draw divider line above status (row 22)
    ui_hline(SCREEN_HEIGHT - 2, 0, SCREEN_WIDTH, CHAR_DIVIDER);

    // Draw status/help text (row 23)
    ui_putstr(SCREEN_HEIGHT - 1, 1, "1-9:Dev  S:Scan  M:Menu", 23);

    // Draw device icons - skip icons obscured by windows (column-major layout)
    for (i = 0; i < g_app.device_count; i++) {
        unsigned int icon_col = ICON_START_COL + (i / ICON_ROWS) * ICON_COL_WIDTH + 1;

        // Skip if icon would be covered by a window
        if (ui_col_covered(icon_col)) continue;

        ui_draw_device_icon(i, &g_app.devices[i]);
    }

    // Reset selection state
    g_sel_visible = 0;
}

// Initialize UI
void ui_init(void) {
    // Character initialization is done in vdp_init()
    // Nothing else needed here currently
}

// Highlight/select a device icon with side brackets (4 wide x 2 tall)
void ui_select_device(unsigned int idx, unsigned int selected) {
    unsigned int row, col;

    if (idx >= g_app.device_count) return;

    // Calculate position (same as ui_draw_device_icon) - column-major
    row = ICON_START_ROW + (idx % ICON_ROWS) * ICON_ROW_HEIGHT;
    col = ICON_START_COL + (idx / ICON_ROWS) * ICON_COL_WIDTH + 1;

    // Clear previous selection if visible
    if (g_sel_visible && !selected) {
        ui_putchar(g_sel_row,     g_sel_col - 1, ' ');  // Left-top
        ui_putchar(g_sel_row + 1, g_sel_col - 1, ' ');  // Left-bottom
        ui_putchar(g_sel_row,     g_sel_col + 2, ' ');  // Right-top
        ui_putchar(g_sel_row + 1, g_sel_col + 2, ' ');  // Right-bottom
        g_sel_visible = 0;
    }

    if (selected) {
        // Draw selection brackets on either side of the 2x2 icon
        // Left side brackets at col-1, Right side at col+2
        ui_putchar(row,     col - 1, CHAR_SEL_LT);  // Left-top bracket
        ui_putchar(row + 1, col - 1, CHAR_SEL_LB);  // Left-bottom bracket
        ui_putchar(row,     col + 2, CHAR_SEL_RT);  // Right-top bracket
        ui_putchar(row + 1, col + 2, CHAR_SEL_RB);  // Right-bottom bracket

        // Remember selection position
        g_sel_visible = 1;
        g_sel_row = row;
        g_sel_col = col;
    }
}

// Clear selection (called when deselecting)
void ui_clear_selection(void) {
    if (g_sel_visible) {
        ui_putchar(g_sel_row,     g_sel_col - 1, ' ');
        ui_putchar(g_sel_row + 1, g_sel_col - 1, ' ');
        ui_putchar(g_sel_row,     g_sel_col + 2, ' ');
        ui_putchar(g_sel_row + 1, g_sel_col + 2, ' ');
        g_sel_visible = 0;
    }
}

// Update status line with message
void ui_status(const char *msg) {
    // Clear status line
    ui_hline(SCREEN_HEIGHT - 1, 0, SCREEN_WIDTH, ' ');
    // Write new message
    ui_putstr(SCREEN_HEIGHT - 1, 1, msg, SCREEN_WIDTH - 2);
}
