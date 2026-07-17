// main.c - TI-99/4A Desktop Environment Entry Point
#include "vdp.h"
#include "kscan.h"
#include "files.h"
#include "config.h"
#include "types.h"
#include "string.h"
#include "ui.h"
#include "input.h"
#include "chars.h"
#include "window.h"
#include "device.h"
#include "prefs.h"

// Global application state
AppState g_app;

// Global color settings (customizable)
unsigned int g_color_bg      = COLOR_BLACK;    // VDP background (register 7)
unsigned int g_color_fg      = COLOR_WHITE;    // Text foreground
unsigned int g_color_desktop = COLOR_DKBLUE;   // Desktop background
unsigned int g_color_icon    = COLOR_WHITE;    // Icon foreground
unsigned int g_color_select  = COLOR_LTGREEN;  // Selection brackets
unsigned int g_color_title_bg= COLOR_CYAN;     // Title bar accent
unsigned int g_color_title_fg= COLOR_WHITE;    // Title bar text
unsigned int g_color_divider = COLOR_DKGREEN;  // Divider line color

// Global title string buffer (customizable via menu, up to 16 chars)
char g_title_string[TITLE_TEXT_LEN + 1] = "TI DESKTOP";

// allows us to restart the app
unsigned int restart_app = 0;

// Clock (RTC) support
unsigned int g_clock_available = 0;  // 1 if CLOCK device found

// Initialize the VDP for Graphics II (bitmap) mode
// We use bitmap mode but treat it like character mode for per-row colors
static void vdp_init(void) {
    unsigned int default_color;

    // Set up Graphics II (bitmap) mode with 8x8 sprites
    set_bitmap(VDP_SPR_8x8);

    // Configure for single pattern/color table (not 3 banks)
    // This lets us use 256 unique characters with per-row colors
    // This keeps the tables at the default locations so gPattern and gColor don't change
    VDP_SET_REGISTER(VDP_REG_CT, 0x9F);   // Color table at >2000, single bank
    VDP_SET_REGISTER(VDP_REG_PDT, 0x00);  // Pattern table at >0000, single bank

    // Set screen background color from global (foreground color is only available in TEXT mode)
    VDP_SET_REGISTER(VDP_REG_COL, g_color_bg);

    // Clear the screen - no need to clear the pattern table before loading it - every byte counts
    vdpmemset(gImage, ' ', SCREEN_WIDTH * SCREEN_HEIGHT);

    // Load custom character patterns and set up per-char colors
    chars_init();
    
}

// Initialize application state
static void app_init(void) {
    unsigned int i;

    g_app.device_count = 0;
    g_app.focus = 0;
    g_app.menu_active = 0;

    // Clear device list
    for (i = 0; i < MAX_DEVICES; i++) {
        g_app.devices[i].name[0] = 0;
        g_app.devices[i].icon = 0;
        g_app.devices[i].flags = DEVICE_NONE;
    }

    // Windows are initialized by window_init()

    // Add cartridge as first device (always present)
    memcpy(&g_app.devices[0].name, "CART", 5);
    g_app.devices[0].icon = CHAR_CART_TL;      // Icon determined by flags in UI
    g_app.devices[0].flags = DEVICE_CART;
    g_app.device_count = 1;
}

// Main entry point
int main(void) {
    // files(2) gives us enough room for a full bitmap image to be loaded
    files(2);

    // this lets us restart after an operation that destroys VDP setup
    for (;;) {
        // Initialize VDP
        vdp_init();

        // Initialize application state (only on first run, not restart)
        // On restart we want to keep all installed devices and window state
        if (!restart_app) {
            app_init();
            window_init();

            // Load saved preferences (colors, title, devices, clock)
            // Holding space at startup skips the load (keeps defaults)
            // On failure the defaults from app_init remain in place
            kscan(KSCAN_MODE_BASIC);
            if (KSCAN_KEY != ' ' && prefs_load()) {
                // Re-apply loaded colors (vdp_init used the defaults)
                VDP_SET_REGISTER(VDP_REG_COL, g_color_bg);
                chars_init();
            }
        } else {
            // Reset non-device state
            g_app.menu_active = 0;
            // Window state is preserved - scroll positions, cursor, files, etc.
        }

        // Draw initial desktop
        ui_draw_desktop();

        // On restart, restore selection reticule and redraw active windows
        if (restart_app) {
            input_restore_selection();
        }

        // clear the restart flag
        restart_app = 0;

        // Main event loop
        {
            unsigned int clock_counter = 0;

            for (;;) {
                // Wait for vblank to pace the loop
                vdpwaitvint();

                // Process keyboard input
                input_process();

                // Update clock display periodically (every ~1 second at 60Hz)
                clock_counter++;
                if (clock_counter >= 60) {
                    clock_counter = 0;
                    clock_update_display();
                }
                
                if (restart_app) {
                    break;
                }
            }
        }
    }

    return 0;
}
