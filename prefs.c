// prefs.c - TI-99/4A Desktop Environment Preference Persistence
// Saves and loads the configuration as a binary blob using the DSR
// LOAD/SAVE opcodes (PROGRAM image style - no OPEN/CLOSE or records)
#include "vdp.h"
#include "files.h"
#include "config.h"
#include "types.h"
#include "device.h"
#include "window.h"
#include "prefs.h"

// Hardcoded preferences file path (for now)
static const char prefs_name[] = "DSK0.DESKTOP_PREFS";
#define PREFS_NAME_LEN  18

// VDP addresses for the prefs PAB and data blob (see config.h memory map)
// These live inside the viewer buffer area (0x29C0-0x32C0), which is only
// used while the text viewer is active - prefs load/save never runs then
#define PREFS_PAB_ADDR  0x29C0
#define PREFS_BUF_ADDR  0x2A00

// Blob identification and sizing
#define PREFS_MAGIC_1   'D'
#define PREFS_MAGIC_2   'P'
#define PREFS_VERSION   3
#define PREFS_HDR_SIZE  31
#define PREFS_DEV_SIZE  10
#define PREFS_MAX_SIZE  (PREFS_HDR_SIZE + PREFS_DEV_SIZE * MAX_DEVICES)

// CPU-side staging buffer for the blob (max 31 + 10*40 = 431 bytes)
static unsigned char prefs_buf[PREFS_MAX_SIZE];

// Scroll position as last saved/loaded - lets window close skip the
// disk write when the scroll position hasn't actually changed
static unsigned int prefs_scroll_saved = FILE_COL_NAME;

// Save preferences to DSK0.DESKTOP_PREFS using the SAVE opcode
// Returns 0 on success, nonzero DSR error on failure
//
// Blob format:
//  Offset  Size   Content
//  0       2      Magic 'D','P'
//  2       1      Format version (3)
//  3       1      Device count n (includes CART at index 0)
//  4       1      Background color (VDP register 7)
//  5       1      Text (foreground) color
//  6       1      Desktop area color
//  7       1      Icon color
//  8       1      Selection highlight color
//  9       1      Title bar background color
//  10      1      Title bar text color
//  11      1      Divider line color
//  12      17     Title bar text (up to 16 chars + null terminator)
//  29      1      Clock available flag (0 or 1)
//  30      1      Default window horizontal scroll position
//  31      10*n   Device entries, each:
//                   +0   8   Device name (null padded); first byte is
//                            OR'd with 0x80 so the literal string (e.g.
//                            "DSK1") never appears in the file - defeats
//                            Classic99's automatic drive-name remapping
//                   +8   1   Icon (top-left character code)
//                   +9   1   Device flags (DEVICE_xxx)
unsigned int prefs_save(void) {
    struct PAB pab;
    unsigned int pos;
    unsigned int i, d;
    unsigned int ended;

    // Header
    prefs_buf[0] = PREFS_MAGIC_1;
    prefs_buf[1] = PREFS_MAGIC_2;
    prefs_buf[2] = PREFS_VERSION;
    prefs_buf[3] = g_app.device_count;

    // Colors
    prefs_buf[4]  = g_color_bg;
    prefs_buf[5]  = g_color_fg;
    prefs_buf[6]  = g_color_desktop;
    prefs_buf[7]  = g_color_icon;
    prefs_buf[8]  = g_color_select;
    prefs_buf[9]  = g_color_title_bg;
    prefs_buf[10] = g_color_title_fg;
    prefs_buf[11] = g_color_divider;

    // Title text (zero padded past the terminator)
    ended = 0;
    for (i = 0; i < TITLE_TEXT_LEN + 1; i++) {
        if (g_title_string[i] == 0) ended = 1;
        prefs_buf[12 + i] = ended ? 0 : g_title_string[i];
    }

    // Clock state
    prefs_buf[29] = g_clock_available ? 1 : 0;

    // Window horizontal scroll default
    prefs_buf[30] = g_default_scroll_x & 0xFF;
    prefs_scroll_saved = g_default_scroll_x;

    // Device entries
    pos = PREFS_HDR_SIZE;
    for (d = 0; d < g_app.device_count; d++) {
        Device *dev = &g_app.devices[d];

        ended = 0;
        for (i = 0; i < 8; i++) {
            if (dev->name[i] == 0) ended = 1;
            prefs_buf[pos + i] = ended ? 0 : dev->name[i];
        }
        // Mask the name so Classic99 doesn't remap drive strings in the file
        prefs_buf[pos] |= 0x80;

        prefs_buf[pos + 8] = dev->icon & 0xFF;
        prefs_buf[pos + 9] = dev->flags & 0xFF;

        pos += PREFS_DEV_SIZE;
    }

    // Copy blob to VDP, then SAVE it as a binary image
    vdpmemcpy(PREFS_BUF_ADDR, prefs_buf, pos);

    preparePAB(&pab, DSR_SAVE, PREFS_BUF_ADDR, PREFS_NAME_LEN, prefs_name);
    pab.RecordNumber = pos;     // Byte count for SAVE

    //pab.OpCode = DSR_SAVE;
    //pab.Status = 0;
    //pab.VDPBuffer = PREFS_BUF_ADDR;
    //pab.RecordLength = 0;
    //pab.CharCount = 0;
    //pab.ScreenOffset = 0;
    //pab.NameLength = PREFS_NAME_LEN;
    //pab.pName = (unsigned char *)prefs_name;

    return dsrlnk(&pab, PREFS_PAB_ADDR);
}

// Load preferences from DSK0.DESKTOP_PREFS using the LOAD opcode
// Replaces colors, title text, clock state, and the entire device list
// Returns 1 if preferences were loaded, 0 if not (missing/invalid file)
unsigned int prefs_load(void) {
    struct PAB pab;
    unsigned int i, d;
    unsigned int count;
    unsigned int pos;

    preparePAB(&pab, DSR_LOAD, PREFS_BUF_ADDR, PREFS_NAME_LEN, prefs_name);
    pab.RecordNumber = PREFS_MAX_SIZE;  // Buffer size limit for LOAD

    //pab.OpCode = DSR_LOAD;
    //pab.Status = 0;
    //pab.VDPBuffer = PREFS_BUF_ADDR;
    //pab.RecordLength = 0;
    //pab.CharCount = 0;
    //pab.ScreenOffset = 0;
    //pab.NameLength = PREFS_NAME_LEN;
    //pab.pName = (unsigned char *)prefs_name;

    if (dsrlnk(&pab, PREFS_PAB_ADDR) != 0) {
        return 0;   // No prefs file (or read error) - keep defaults
    }

    // Pull the whole blob back from VDP and validate it
    vdpmemread(PREFS_BUF_ADDR, prefs_buf, PREFS_MAX_SIZE);

    if (prefs_buf[0] != PREFS_MAGIC_1 || prefs_buf[1] != PREFS_MAGIC_2 ||
        prefs_buf[2] != PREFS_VERSION) {
        return 0;
    }

    count = prefs_buf[3];
    if (count == 0 || count > MAX_DEVICES) {
        return 0;
    }

    // Colors
    g_color_bg      = prefs_buf[4]  & 0x0F;
    g_color_fg      = prefs_buf[5]  & 0x0F;
    g_color_desktop = prefs_buf[6]  & 0x0F;
    g_color_icon    = prefs_buf[7]  & 0x0F;
    g_color_select  = prefs_buf[8]  & 0x0F;
    g_color_title_bg= prefs_buf[9]  & 0x0F;
    g_color_title_fg= prefs_buf[10] & 0x0F;
    g_color_divider = prefs_buf[11] & 0x0F;

    // Title text
    for (i = 0; i < TITLE_TEXT_LEN; i++) {
        g_title_string[i] = prefs_buf[12 + i];
    }
    g_title_string[TITLE_TEXT_LEN] = 0;

    // Clock state
    g_clock_available = prefs_buf[29] ? 1 : 0;

    // Window horizontal scroll default (clamp bad values to the name column)
    g_default_scroll_x = prefs_buf[30];
    if (g_default_scroll_x >= FILE_LINE_LEN) {
        g_default_scroll_x = FILE_COL_NAME;
    }
    prefs_scroll_saved = g_default_scroll_x;

    // Device entries (replaces the whole list, including CART at index 0)
    pos = PREFS_HDR_SIZE;
    for (d = 0; d < count; d++) {
        Device *dev = &g_app.devices[d];

        for (i = 0; i < 7; i++) {
            dev->name[i] = prefs_buf[pos + i];
        }
        dev->name[7] = 0;
        // Remove the anti-remapping mask from the first name byte
        dev->name[0] &= 0x7F;

        dev->icon = prefs_buf[pos + 8];
        dev->flags = prefs_buf[pos + 9];

        pos += PREFS_DEV_SIZE;
    }
    g_app.device_count = count;

    return 1;
}

// Re-save preferences if the scroll position changed since the last
// save or load - called when a window closes
void prefs_save_scroll(void) {
    if (g_default_scroll_x != prefs_scroll_saved) {
        prefs_save();
    }
}
