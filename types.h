// types.h - TI-99/4A Desktop Environment Data Structures
#ifndef TYPES_H
#define TYPES_H

#include "config.h"

// Device entry - represents a device icon on the desktop
typedef struct {
    unsigned int cru_base;      // CRU address (0 for cartridge)
    unsigned int name;          // Device name as packed chars (e.g., 'D'<<8|'1')
    unsigned int icon;          // Character code for icon (use int to avoid char issues)
    unsigned int flags;         // DEVICE_DISK, DEVICE_CART, etc
} Device;

// Directory entry - one file in a directory listing
typedef struct {
    unsigned int name0;         // Filename chars 0-1 packed
    unsigned int name1;         // Filename chars 2-3 packed
    unsigned int name2;         // Filename chars 4-5 packed
    unsigned int name3;         // Filename chars 6-7 packed
    unsigned int name4;         // Filename chars 8-9 packed
    unsigned int type;          // File type (FTYPE_xxx)
    unsigned int sectors;       // File size in sectors
    unsigned int reclen;        // Record length (for fixed)
} DirEntry;

// Window state - manages an open window
typedef struct {
    unsigned int x;             // Window X position
    unsigned int y;             // Window Y position
    unsigned int w;             // Window width
    unsigned int h;             // Window height
    unsigned int scroll;        // Current scroll offset
    unsigned int selected;      // Currently selected item index
    unsigned int count;         // Total number of items
    Device *device;             // Device this window shows (NULL = desktop)
} Window;

// Application state
typedef struct {
    unsigned int device_count;  // Number of detected devices
    unsigned int focus;         // 0 = desktop, 1+ = window index
    unsigned int menu_active;   // 1 if menu is showing
    Device devices[MAX_DEVICES];
    Window windows[MAX_WINDOWS];
    DirEntry *dir_cache;        // Points to VDP_DIRBUF area
} AppState;

// Global application state
extern AppState g_app;

// Global color settings (for customization)
extern unsigned int g_color_bg;       // VDP background color (register 7)
extern unsigned int g_color_fg;       // Foreground (text) color
extern unsigned int g_color_desktop;  // Desktop area background color
extern unsigned int g_color_icon;     // Icon color
extern unsigned int g_color_select;   // Selection highlight color
extern unsigned int g_color_title;    // Title bar accent color (cyan)

// Global title string (for customization)
extern const char *g_title_string;

// Convert ASCII to title bar mini-font character
extern unsigned int char_to_title(unsigned int c);

#endif // TYPES_H
