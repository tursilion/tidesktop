// types.h - TI-99/4A Desktop Environment Data Structures
#ifndef TYPES_H
#define TYPES_H

#include "config.h"

// Focus constants
#define FOCUS_DESKTOP   0
#define FOCUS_WINDOW1   1   // Right half window
#define FOCUS_WINDOW2   2   // Left half window

// File type enum
#define FILE_TYPE_PROGRAM  0
#define FILE_TYPE_INTFIX   1
#define FILE_TYPE_INTVAR   2
#define FILE_TYPE_DISFIX   3
#define FILE_TYPE_DISVAR   4
#define FILE_TYPE_GROM     5   // GROM-based cartridge program
#define FILE_TYPE_ROM      6   // ROM-based cartridge program

// Device entry - represents a device icon on the desktop
typedef struct {
    unsigned int cru_base;      // CRU address (0 for cartridge)
    char name[8];               // Device name (up to 7 chars + null)
    unsigned int icon;          // Character code for icon (use int to avoid char issues)
    unsigned int flags;         // DEVICE_DISK, DEVICE_CART, etc
} Device;

// File entry - one file in a directory listing
typedef struct {
    char name[32];              // Filename (up to 32 chars for LFN support)
    unsigned int type;          // File type (FILE_TYPE_xxx)
    unsigned int size;          // File size in sectors/bytes
    unsigned int rec_len;       // Record length (0 for PROGRAM)
} FileEntry;

// Window state - manages an open window
typedef struct {
    unsigned int active;        // 1 if window is open, 0 if closed
    unsigned int x;             // Window X position on screen
    unsigned int y;             // Window Y position on screen
    unsigned int w;             // Window width on screen
    unsigned int h;             // Window height on screen
    unsigned int scroll_x;      // Horizontal scroll offset (0-based column)
    unsigned int scroll_y;      // Vertical scroll offset (0-based row)
    unsigned int cursor_y;      // Currently selected row (0-based)
    unsigned int file_count;    // Number of files in buffer
    unsigned int page_start;    // Starting file index for current page
    Device *device;             // Device this window shows
    FileEntry files[WIN_MAX_FILES];  // File entries for this window
} Window;

// Application state
typedef struct {
    unsigned int device_count;  // Number of detected devices
    unsigned int focus;         // FOCUS_DESKTOP, FOCUS_WINDOW1, FOCUS_WINDOW2
    unsigned int menu_active;   // 1 if menu is showing
    unsigned int win_count;     // Number of open windows (0-2)
    Device devices[MAX_DEVICES];
    Window windows[MAX_WINDOWS];
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
