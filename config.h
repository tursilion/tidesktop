// config.h - TI-99/4A Desktop Environment Configuration
#ifndef CONFIG_H
#define CONFIG_H

// VDP Memory Layout (Graphics I Mode)
// 0000 PDT (0x800 bytes)
// 0800 
// 1800 SIT (0x300 bytes) and SDT (0x800 bytes, overlapping)
// 1B00 SAL (0x80 bytes)
// 1B80
// 2000 CT  (0x800 bytes)
// 2800 CLOCK_PAB_ADDR
// 2820 CLOCK_BUF_ADDR (max 96 bytes)
// 2880 DIR_PAB_ADDR/VIEW_PAB_ADDR
// 28C0 DIR_BUF_ADDR/VIEW_BUF_ADDR (max 256 bytes)
// 29C0 Viewer buffer (may be resused when viewer not active - 18 rows * 128 bytes)
// 32C0
// 3980 RESERVED FOR DISK INCLUDING CF7 (with files(2))

// Screen dimensions (Graphics I)
#define SCREEN_WIDTH    32
#define SCREEN_HEIGHT   24

// Maximum limits
#define MAX_DEVICES     40      // 8x5 grid on desktop (hotkeys for first 9)
#define MAX_DIR_ENTRIES 64      // Max files shown in a directory
#define MAX_WINDOWS     2       // Two non-overlapped windows max
#define MAX_SCAN_DEVICES 64     // Max devices found during scan

// Desktop icon grid layout
#define ICON_COLS       8       // Icons per row
#define ICON_ROWS       5       // Rows of icons
#define ICON_COL_WIDTH  4       // Columns per icon slot
#define ICON_ROW_HEIGHT 4       // Rows per icon slot (2 icon + 1 label + 1 space)
#define ICON_START_ROW  2       // First row for icons
#define ICON_START_COL  0       // First column for icons

// Window configuration
// Window 1 opens on right half, Window 2 opens on left half
#define WIN_WIDTH       16      // Each window is half screen width
#define WIN_HEIGHT      20      // Available height (rows 2-21, leaving title+dividers)
#define WIN_CONTENT_W   14      // Content width (minus borders)
#define WIN_CONTENT_H   18      // Content height (minus title+border)
#define WIN_MAX_FILES   20      // Max files per window page
#define WIN_BUF_COLS    64      // Internal buffer width (for panning)
#define WIN_BUF_ROWS    20      // Internal buffer height

// File display format: <size 4> <type 5> <filename 32> = 43 chars
// Format: "999K PROG  FILENAME..." or "999K DV128 FILENAME..."
#define FILE_COL_SIZE   0       // Size column start
#define FILE_COL_TYPE   5       // Type column start (after "999K ")
#define FILE_COL_NAME   11      // Name column start (after "DV128 ")
#define FILE_NAME_LEN   32      // Max filename length (display)
#define FILE_LINE_LEN   43      // Total line length
#define PATH_MAX_LEN    128     // Max internal path/filename buffer size
#define TITLE_TEXT_LEN  16      // Max title bar text length (user configurable)

// Window positions (Y is row 2, below title bar and divider)
#define WIN1_X          16      // Right half (dual window mode)
#define WIN1_Y          2
#define WIN2_X          0       // Left half (dual window mode)
#define WIN2_Y          2

// Expanded window (single window mode) - leaves first icon column visible
#define WIN_EXPANDED_X  7       // Start after first icon column
#define WIN_EXPANDED_W  25      // Full width minus icon column

// Icon characters (2x2 icons, 4 chars each)
// Floppy disk icon (chars 128-131)
#define CHAR_DISK_TL    128
#define CHAR_DISK_TR    129
#define CHAR_DISK_BL    130
#define CHAR_DISK_BR    131

// Cartridge icon (chars 132-135)
#define CHAR_CART_TL    132
#define CHAR_CART_TR    133
#define CHAR_CART_BL    134
#define CHAR_CART_BR    135

// Hard drive icon (chars 136-139)
#define CHAR_HD_TL      136
#define CHAR_HD_TR      137
#define CHAR_HD_BL      138
#define CHAR_HD_BR      139

// RAM disk icon (chars 140-143)
#define CHAR_RAM_TL     140
#define CHAR_RAM_TR     141
#define CHAR_RAM_BL     142
#define CHAR_RAM_BR     143

// Futuristic title bar (chars 144-147)
#define CHAR_TITLE_L    144     // Title bar left cap (angular)
#define CHAR_TITLE_FILL 145     // Title bar fill
#define CHAR_TITLE_R    146     // Title bar right cap (angular)
#define CHAR_TITLE_TEXT 147     // Title bar text area (recessed)

// Divider and window elements (chars 148-151)
#define CHAR_DIVIDER    148     // Divider line (for top/bottom)
#define CHAR_WIN_V      149     // Vertical line
#define CHAR_WIN_TL     150     // Window corner top-left
#define CHAR_WIN_TR     151     // Window corner top-right

// File icons (chars 152-155)
#define CHAR_FILE_TL    152
#define CHAR_FILE_TR    153
#define CHAR_FILE_BL    154
#define CHAR_FILE_BR    155

// Program file icons (chars 156-159)
#define CHAR_PROG_TL    156
#define CHAR_PROG_TR    157
#define CHAR_PROG_BL    158
#define CHAR_PROG_BR    159

// Selection brackets - side brackets for 4x2 selection (chars 160-163)
#define CHAR_SEL_LT     160     // Selection left-top
#define CHAR_SEL_LB     161     // Selection left-bottom
#define CHAR_SEL_RT     162     // Selection right-top
#define CHAR_SEL_RB     163     // Selection right-bottom

// Additional window elements (chars 164-167)
#define CHAR_WIN_BL     164     // Window corner bottom-left
#define CHAR_WIN_BR     165     // Window corner bottom-right
#define CHAR_WIN_H      166     // Horizontal line (window)
#define CHAR_MENU_ARROW 167     // Menu arrow indicator

// Title bar mini-font (chars 168-204)
// Small 4-row font for inset title bar text
// 168-177: digits 0-9
// 178-203: letters A-Z
// 204: space
#define CHAR_TITLE_FONT_BASE  168
#define CHAR_TITLE_FONT_SPACE 204

// Device type flags
#define DEVICE_NONE     0x00
#define DEVICE_CART     0x01    // Cartridge pseudo-device
#define DEVICE_DISK     0x02    // Floppy disk
#define DEVICE_HD       0x04    // Hard drive / CF card
#define DEVICE_RAMDISK  0x08    // RAM disk

// File type constants (from TI disk format)
#define FTYPE_DIS_FIX   0x00    // Display Fixed
#define FTYPE_DIS_VAR   0x01    // Display Variable
#define FTYPE_INT_FIX   0x02    // Internal Fixed
#define FTYPE_INT_VAR   0x03    // Internal Variable
#define FTYPE_PROGRAM   0x05    // Program file

// Hotkey codes (returned by kscan in mode 5)
#define KEY_1           '1'
#define KEY_9           '9'
#define KEY_M           'M'
#define KEY_R           'R'
#define KEY_C           'C'
#define KEY_V           'V'
#define KEY_D           'D'
#define KEY_S           'S'
#define KEY_E           'E'     // Fctn-E = up
#define KEY_X           'X'     // Fctn-X = down

#endif // CONFIG_H
