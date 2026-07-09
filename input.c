// input.c - TI-99/4A Desktop Environment Input Handling
#include "kscan.h"
#include "config.h"
#include "types.h"

// Forward declarations from ui.c
extern void ui_select_device(unsigned int idx, unsigned int selected);
extern void ui_clear_selection(void);
extern void ui_status(const char *msg);
extern void ui_draw_desktop(void);

// Currently selected device on desktop (-1 = none selected)
static int g_selected = -1;
static int g_has_selection = 0;

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
    if (!g_has_selection) return;
    if (g_selected >= 4) {
        ui_select_device(g_selected, 0);
        g_selected -= 4;
        ui_select_device(g_selected, 1);
    }
}

static void input_nav_down(void) {
    if (!g_has_selection) return;
    if (g_selected + 4 < (int)g_app.device_count) {
        ui_select_device(g_selected, 0);
        g_selected += 4;
        ui_select_device(g_selected, 1);
    }
}

static void input_nav_left(void) {
    if (!g_has_selection) return;
    if (g_selected > 0) {
        ui_select_device(g_selected, 0);
        g_selected--;
        ui_select_device(g_selected, 1);
    }
}

static void input_nav_right(void) {
    if (!g_has_selection) return;
    if (g_selected + 1 < (int)g_app.device_count) {
        ui_select_device(g_selected, 0);
        g_selected++;
        ui_select_device(g_selected, 1);
    }
}

// Handle Enter - open selected device
static void input_open(void) {
    // TODO: Implement device opening
    ui_status("Opening device...");
}

// Handle Scan - enumerate devices via CRU
static void input_scan(void) {
    // TODO: Implement CRU scanning
    ui_status("Scanning devices...");
}

// Handle Menu
static void input_menu(void) {
    // TODO: Implement menu display
    ui_status("Menu not implemented");
}

// Handle Fctn-9 (back/close/deselect)
static void input_back(void) {
    if (g_app.focus > 0) {
        // Close current window, return to desktop
        g_app.focus = 0;
        ui_draw_desktop();
        if (g_has_selection) {
            ui_select_device(g_selected, 1);
        }
    } else if (g_has_selection) {
        // On desktop with selection - deselect
        ui_clear_selection();
        g_has_selection = 0;
        g_selected = -1;
    }
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
        return;
    }

    // Handle Fctn-9 (close/back)
    if (key == KEY_BACK) {
        input_back();
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
    if (key >= '1' && key <= '9') {
        input_select_device(key - '1');
        return;
    }

    // Enter - open
    if (key == KEY_ENTER) {
        input_open();
        return;
    }

    // M - menu
    if (key == 'M' || key == 'm') {
        input_menu();
        return;
    }

    // S - scan
    if (key == 'S' || key == 's') {
        input_scan();
        return;
    }
}
