// main.c - TI-99/4A Desktop Environment Entry Point
#include "vdp.h"
#include "kscan.h"
#include "config.h"
#include "types.h"

// Forward declarations from other modules
extern void ui_init(void);
extern void ui_draw_desktop(void);
extern void input_process(void);
extern void chars_init(void);
extern void window_init(void);

// Global application state
AppState g_app;

// Global color settings (customizable)
unsigned int g_color_bg      = COLOR_BLACK;    // VDP background (register 7)
unsigned int g_color_fg      = COLOR_WHITE;    // Text foreground
unsigned int g_color_desktop = COLOR_DKBLUE;   // Desktop background
unsigned int g_color_icon    = COLOR_WHITE;    // Icon foreground
unsigned int g_color_select  = COLOR_LTGREEN;  // Selection brackets
unsigned int g_color_title   = COLOR_CYAN;     // Title bar accent
unsigned int g_color_divider = COLOR_DKGREEN;  // Divider line color

// Global title string (customizable)
const char *g_title_string = "TI DESKTOP";

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

    // Set default colors for all characters using globals
    // In bitmap mode, each char has 8 color bytes (one per row)
    default_color = (g_color_fg << 4) | g_color_desktop;
    vdpmemset(gColor, default_color, 256 * 8);

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
        g_app.devices[i].cru_base = 0;
        g_app.devices[i].name[0] = 0;
        g_app.devices[i].icon = 0;
        g_app.devices[i].flags = DEVICE_NONE;
    }

    // Windows are initialized by window_init()

    // Add cartridge as first device (always present)
    g_app.devices[0].cru_base = 0;
    g_app.devices[0].name[0] = 'C';
    g_app.devices[0].name[1] = 'A';
    g_app.devices[0].name[2] = 'R';
    g_app.devices[0].name[3] = 'T';
    g_app.devices[0].name[4] = 0;
    g_app.devices[0].icon = CHAR_CART_TL;      // Icon determined by flags in UI
    g_app.devices[0].flags = DEVICE_CART;
    g_app.device_count = 1;
}

// Main entry point
int main(void) {
    // Initialize VDP
    vdp_init();

    // Initialize application state
    app_init();

    // Initialize window system
    window_init();

    // Initialize UI
    ui_init();

    // Draw initial desktop
    ui_draw_desktop();

    // Main event loop
    for (;;) {
        // Wait for vblank to pace the loop
        vdpwaitvint();

        // Process keyboard input
        input_process();
    }

    return 0;
}
