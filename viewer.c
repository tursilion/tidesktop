// TODO: DF128 failed to open (tried to open it as DF80)
// libti99all is not returning DSR errors, or we aren't seeing them

// viewer.c - TI-99/4A Desktop Environment Text File Viewer
#include "config.h"
#include "types.h"
#include "vdp.h"
#include "kscan.h"
#include "files.h"

// External functions
extern void ui_status(const char *msg);

// Viewer PAB addresses (same as directory PAB)
#define VIEW_PAB_ADDR   0x2800
#define VIEW_BUF_ADDR   0x28C0

// Bitmap viewer PAB address (separate from text viewer)
#define BMP_PAB_ADDR    0x3800

// Viewer window dimensions
#define VIEW_X          0
#define VIEW_Y          2
#define VIEW_W          32
#define VIEW_H          20
#define VIEW_ROWS       18      // Content rows (minus title and bottom border)

// VDP addresses for viewer record storage
// Use VDP memory above the screen to store records
#define VIEW_REC_BASE   0x29c0      // Start of record storage in VDP
#define VIEW_REC_SIZE   128         // based on desired size, and foreign files being DF128

// Viewer state (minimal CPU RAM usage)
 struct {
    unsigned int is_open;           // File is currently open
    unsigned int is_variable;       // 1 = variable length records
    unsigned int rec_len;           // Record length (actual from file)
    unsigned int scroll_x;          // Horizontal scroll position
    unsigned int current_rec;       // Current first record on screen
    unsigned int at_eof;            // Reached end of file
    unsigned int rec_count;         // Number of records in buffer
    unsigned int rec_lengths[VIEW_ROWS]; // Actual length of each record (in VDP)
    char filename[PATH_MAX_LEN];     // Full path to file
} g_viewer;

static void viewer_close_file(void);

// Draw the viewer frame
static void viewer_draw_frame(const char *title) {
    unsigned int i;
    unsigned int addr;

    // Top border
    vdpscreenchar(VDP_SCREEN_POS(VIEW_Y, VIEW_X), CHAR_WIN_TL);
    hchar(VIEW_Y, VIEW_X + 1, CHAR_WIN_H, VIEW_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(VIEW_Y, VIEW_X + VIEW_W - 1), CHAR_WIN_TR);

    // Title (truncate to fit)
    if (title) {
        unsigned int title_len = 0;
        while (title[title_len] && title_len < VIEW_W - 4) title_len++;
        addr = gImage + VDP_SCREEN_POS(VIEW_Y, VIEW_X + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        for (i = 0; i < title_len; i++) {
            VDPWD(title[i]);
        }
    }

    // Side borders
    for (i = 1; i < VIEW_H - 1; i++) {
        vdpscreenchar(VDP_SCREEN_POS(VIEW_Y + i, VIEW_X), CHAR_WIN_V);
        vdpscreenchar(VDP_SCREEN_POS(VIEW_Y + i, VIEW_X + VIEW_W - 1), CHAR_WIN_V);
    }

    // Bottom border
    vdpscreenchar(VDP_SCREEN_POS(VIEW_Y + VIEW_H - 1, VIEW_X), CHAR_WIN_BL);
    hchar(VIEW_Y + VIEW_H - 1, VIEW_X + 1, CHAR_WIN_H, VIEW_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(VIEW_Y + VIEW_H - 1, VIEW_X + VIEW_W - 1), CHAR_WIN_BR);

    // Clear interior
    for (i = 1; i < VIEW_H - 1; i++) {
        hchar(VIEW_Y + i, VIEW_X + 1, ' ', VIEW_W - 2);
    }
}

// Draw the current page of records
static void viewer_draw_content(void) {
    unsigned int i;
    unsigned int row;
    unsigned int addr;
    const unsigned int visible_w = VIEW_W - 2;  // Content width
    unsigned int rec_len;
    unsigned int vdp_rec_addr;
    unsigned char ch;
    char lclbuf[VIEW_W-2];  // storage for the content per line

    // we already hchar each line, so we don't need to pad if it's shorter
    for (i = 0; i < VIEW_ROWS; i++) {
        row = VIEW_Y + 1 + i;

        // Clear the row
        hchar(row, VIEW_X + 1, ' ', visible_w);

        if (i < g_viewer.rec_count) {
            rec_len = g_viewer.rec_lengths[i];
            vdp_rec_addr = VIEW_REC_BASE + (i * VIEW_REC_SIZE) + g_viewer.scroll_x;
            int lclwidth = VIEW_W-2;
            if (lclwidth + g_viewer.scroll_x > rec_len) {
                lclwidth = rec_len - (g_viewer.scroll_x);
                if (lclwidth < 0) lclwidth = 0;
            }

            if (lclwidth > 0) {
                // Draw visible portion based on scroll_x
                // note that VDP is a memory mapped device with a single
                // hardware register - you can't read and write without
                // both changing the address on this machine. Fastest way
                // is to cache a line in and write it back out. We don't need
                // to filter characters, none have side effects.
                addr = gImage + VDP_SCREEN_POS(row, VIEW_X + 1);
                vdpmemread(vdp_rec_addr, lclbuf, lclwidth);
                vdpmemcpy(addr, lclbuf, lclwidth);
            }
        }
    }
}

// load the default data into a PAB
static void prepare_pab(struct PAB *ppab) {
    if (g_viewer.is_variable) {
        ppab->Status = DSR_TYPE_VARIABLE | DSR_TYPE_DISPLAY | DSR_TYPE_INPUT;
    } else {
        ppab->Status = DSR_TYPE_DISPLAY | DSR_TYPE_INPUT;
    }
    ppab->VDPBuffer = VIEW_BUF_ADDR;
    ppab->RecordLength = g_viewer.rec_len;
    ppab->CharCount = 0;
    ppab->RecordNumber = 0;
    ppab->ScreenOffset = 0;
    ppab->NameLength = 0;
    ppab->pName = (unsigned char *)g_viewer.filename;
}

// opens the file after the global structure has been setup, so we can re-open
// if EOF happens.
static unsigned int viewer_reopen_file() {
    struct PAB pab;
    unsigned char result;

    prepare_pab(&pab);
    
    // Set up PAB for OPEN
    pab.OpCode = DSR_OPEN;
    result = dsrlnk(&pab, VIEW_PAB_ADDR);
    if (result != 0) {
        return 0;
    }

    // Read back actual record length from VDP
    g_viewer.rec_len = vdpreadchar(VIEW_PAB_ADDR + 4);
    if (g_viewer.rec_len == 0) {
        g_viewer.rec_len = 80;  // Default if not set - this will likely fail
    }
    if (g_viewer.rec_len > VIEW_REC_SIZE) {
        // record length too long
        viewer_close_file();
        return 0;
    }

    g_viewer.is_open = 1;
    g_viewer.scroll_x = 0;
    g_viewer.current_rec = 0;
    g_viewer.at_eof = 0;
    g_viewer.rec_count = 0;

    return 1;
}

// Open a file for viewing
// Returns 1 on success, 0 on failure
static unsigned int viewer_open_file(const char *path, unsigned int is_variable, unsigned int rec_len) {
    struct PAB pab;
    unsigned char result;
    unsigned int i;
    unsigned int path_len;

    // Copy filename
    for (path_len = 0; path[path_len] && path_len < 127; path_len++) {
        g_viewer.filename[path_len] = path[path_len];
    }
    g_viewer.filename[path_len] = 0;
    g_viewer.is_variable = is_variable;
    
    return viewer_reopen_file();
}

// Close the current file
static void viewer_close_file(void) {
    struct PAB pab;

    if (!g_viewer.is_open) return;

    prepare_pab(&pab);

    pab.OpCode = DSR_CLOSE;
    dsrlnk(&pab, VIEW_PAB_ADDR);
    g_viewer.is_open = 0;
}

// Read a page of records starting at record_num
// For variable files, if record_num != current position, must rewind
// Returns number of records read
static unsigned int viewer_read_page(unsigned int record_num) {
    struct PAB pab;
    unsigned char result;
    unsigned int i;
    unsigned int count = 0;
    unsigned int char_count;
    unsigned int vdp_rec_addr;
    unsigned int copy_len;
    unsigned int reopened = 0;
    char lclbuf[256];

    if (!g_viewer.is_open) {
        if (g_viewer.at_eof) {
            // if we ran off the end of the file, just re-open it
            if (!viewer_reopen_file()) {
                return 0;
            }
            // we need to scan up to the correct record
            reopened = 1;
        } else {
            // some other condition
            return 0;
        }
    }
    
    prepare_pab(&pab);

    // For fixed records, we can set RecordNumber directly
    // For variable records, we need to handle differently
    if (!g_viewer.is_variable) {
        // Fixed: set record number directly
        pab.RecordNumber = record_num;
    } else {
        // Variable: if going backwards, rewind and skip forward
        if (record_num < g_viewer.current_rec || record_num == 0 || reopened) {
            // Rewind the file
            pab.OpCode = DSR_REWIND;
            pab.RecordNumber = 0;
            dsrlnk(&pab, VIEW_PAB_ADDR);

            // Skip to desired record
            for (i = 0; i < record_num; i++) {
                pab.OpCode = DSR_READ;
                pab.CharCount = 0;
                result = dsrlnk(&pab, VIEW_PAB_ADDR);
                if (result != 0) {
                    // Error or EOF while skipping
                    // disk errors on the TI close the file
                    // note that we probably read some of it!
                    // This probably shouldn't happen...
                    g_viewer.at_eof = 1;
                    g_viewer.is_open = 0;
                    g_viewer.rec_count = i-1;
                    return 1;
                }
            }
        }
        // For sequential forward read, we just continue
    }

    g_viewer.current_rec = record_num;
    g_viewer.at_eof = 0;

    // Read records into VDP buffer area
    for (count = 0; count < VIEW_ROWS; count++) {
        pab.OpCode = DSR_READ;
        pab.CharCount = 0;
        if (!g_viewer.is_variable) {
            pab.RecordNumber = record_num + count;
        }

        result = dsrlnk(&pab, VIEW_PAB_ADDR);
        if (result != 0) {
            // End of file or error
            g_viewer.is_open = 0;
            g_viewer.rec_count = pab.RecordNumber;
            g_viewer.at_eof = 1;
            break;
        }

        // Read CharCount from VDP
        char_count = vdpreadchar(VIEW_PAB_ADDR + 5);
        if (char_count > VIEW_REC_SIZE) char_count = VIEW_REC_SIZE;

        // Store record length
        g_viewer.rec_lengths[count] = char_count;

        // Copy record data from file buffer to viewer storage in VDP
        // Limit to VIEW_REC_SIZE to save VDP space
        copy_len = (char_count > VIEW_REC_SIZE) ? VIEW_REC_SIZE : char_count;
        vdp_rec_addr = VIEW_REC_BASE + (count * VIEW_REC_SIZE);

        // copying one byte at a time is 3 times slower than using a local buffer
        // every address set takes two bytes, plus one for the byte
        if (copy_len > 0) {
            // Copy from VIEW_BUF_ADDR to vdp_rec_addr within VDP
            // Read from file buffer, write to record storage
            vdpmemread(VIEW_BUF_ADDR, lclbuf, copy_len);
            vdpmemcpy(vdp_rec_addr, lclbuf, copy_len);
        }
    }

    g_viewer.rec_count = count;
    return count;
}

// Scroll left
static void viewer_scroll_left(void) {
    if (g_viewer.scroll_x > 0) {
        g_viewer.scroll_x--;
        viewer_draw_content();
    }
}

// Scroll right
static void viewer_scroll_right(void) {
    // Max scroll is record length minus visible width
    // Use VIEW_REC_SIZE as max since that's what we store
    unsigned int max_len = (g_viewer.rec_len > VIEW_REC_SIZE) ? VIEW_REC_SIZE : g_viewer.rec_len;
    unsigned int max_scroll = (max_len > VIEW_W - 2) ? (max_len - (VIEW_W - 2)) : 0;
    if (g_viewer.scroll_x < max_scroll) {
        g_viewer.scroll_x++;
        viewer_draw_content();
    }
}

// Page up
static void viewer_page_up(void) {
    if (g_viewer.current_rec >= VIEW_ROWS) {
        viewer_read_page(g_viewer.current_rec - VIEW_ROWS);
        viewer_draw_content();
    } else if (g_viewer.current_rec > 0) {
        viewer_read_page(0);
        viewer_draw_content();
    }
}

// Page down
static void viewer_page_down(void) {
    if (!g_viewer.at_eof && g_viewer.rec_count == VIEW_ROWS) {
        viewer_read_page(g_viewer.current_rec + VIEW_ROWS);
        viewer_draw_content();
    }
}

// Extract filename from full path for title
static void viewer_get_title(const char *path, char *title, unsigned int max_len) {
    unsigned int i, last_dot = 0;
    unsigned int len = 0;

    // Find last dot (filename starts after it)
    for (i = 0; path[i]; i++) {
        if (path[i] == '.') {
            last_dot = i + 1;
        }
        len = i + 1;
    }

    // Copy from last_dot to end
    for (i = 0; i < max_len - 1 && path[last_dot + i]; i++) {
        title[i] = path[last_dot + i];
    }
    title[i] = 0;
}

// Main viewer function - opens file and handles input until closed
// path: full path to file (e.g., "DSK1.MYFILE")
// is_variable: 1 for DIS/VAR, 0 for DIS/FIX
// rec_len: record length (0 for auto-detect with variable)
void viewer_view_file(const char *path, unsigned int is_variable, unsigned int rec_len) {
    unsigned int key, lastkey;
    char title[32];
    extern void ui_draw_desktop(void);
    extern void window_redraw_all(void);

    // Open the file
    if (!viewer_open_file(path, is_variable, rec_len)) {
        ui_status("Cannot open file");
        return;
    }

    // Get title from filename
    viewer_get_title(path, title, 28);

    // Draw viewer frame
    viewer_draw_frame(title);

    // Read and display first page
    viewer_read_page(0);
    viewer_draw_content();

    // Show help
    ui_status("Spc:page F4/5/6:scroll F9 Back");

    // Input loop
    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        lastkey = key;
        key = KSCAN_KEY;
    
        if (key == KSCAN_NOKEY) continue;

        // Small delay for debounce
        {
            volatile unsigned int d;
            for (d = 0; d < 500; d++);
        }

        if (key == KEY_BACK) {
            // Close viewer
            break;
        }

        if (key == KEY_LEFT) {
            viewer_scroll_left();
        }

        if (key == KEY_RIGHT) {
            viewer_scroll_right();
        }

        if (key == ' ') {
            // Scroll down one line
            if (!g_viewer.at_eof) {
                viewer_read_page(g_viewer.current_rec + 1);
                viewer_draw_content();
            }
        }
        
        if (key == KEY_BEGIN) {
            // don't repeat this one
            if (lastkey != KEY_BEGIN) {
                // page over half a screen
                // Max scroll is record length minus visible width
                // Use VIEW_REC_SIZE as max since that's what we store
                unsigned int max_len = (g_viewer.rec_len > VIEW_REC_SIZE) ? VIEW_REC_SIZE : g_viewer.rec_len;
                unsigned int max_scroll = (max_len > VIEW_W - 2) ? (max_len - (VIEW_W - 2)) : 0;
                int newscroll;
                if (g_viewer.scroll_x == max_scroll) {
                    newscroll = 0;
                } else {
                    newscroll = g_viewer.scroll_x+20;
                }
                
                if (newscroll > max_scroll) {
                    newscroll = max_scroll;
                }
                if (g_viewer.scroll_x != newscroll) {
                    g_viewer.scroll_x = newscroll;
                    viewer_draw_content();
                }
            }
        }

        if (key == KEY_PROCEED) {
            // Page up (Fctn-6)
            viewer_page_up();
        }

        if (key == KEY_CLEAR) {
            // Page down (Fctn-4)
            viewer_page_down();
        }
    }

    // Close file
    viewer_close_file();

    // Redraw desktop and windows
    ui_draw_desktop();
    window_redraw_all();
}

// Check if filename ends with _P (bitmap pattern file)
// Returns 1 if it ends with _P, 0 otherwise
unsigned int viewer_is_bitmap(const char *path) {
    unsigned int len = 0;

    // Find length
    while (path[len]) len++;

    // Check for _P suffix (need at least 2 chars)
    if (len < 2) return 0;

    return (path[len-2] == '_' && path[len-1] == 'P');
}

// Display a bitmap image (pattern file ending in _P)
// Loads pattern to >0000, changes _P to _C and loads color to >2000
// Sets restart_app flag when done since VDP is reconfigured
void viewer_show_bitmap(const char *path) {
    struct PAB pab;
    unsigned char result;
    char filename[PATH_MAX_LEN];
    unsigned int i, len;
    extern unsigned int restart_app;

    // Copy filename
    for (len = 0; path[len] && len < PATH_MAX_LEN - 1; len++) {
        filename[len] = path[len];
    }
    filename[len] = 0;

    // Set up normal bitmap mode (not the half-mode desktop uses)
    set_bitmap(VDP_SPR_8x8);

    // Clear screen
    vdpmemset(gImage, 0, 768);

    // Set up PAB for LOAD - pattern data
    pab.OpCode = DSR_LOAD;
    pab.Status = 0;
    pab.VDPBuffer = 0x0000;         // Pattern table at >0000
    pab.RecordLength = 0;
    pab.CharCount = 0;
    pab.RecordNumber = 6144;        // Max bytes to load
    pab.ScreenOffset = 0;
    pab.NameLength = 0;
    pab.pName = (unsigned char *)filename;

    // Load pattern data
    result = dsrlnk(&pab, BMP_PAB_ADDR);
    if (result != 0) {
        // Pattern load failed - can't display anything useful
        // Fill with a simple pattern so user sees something
        vdpmemset(0x0000, 0xAA, 6144);
    }

    // Change _P to _C for color file
    if (len >= 2) {
        filename[len-1] = 'C';
    }

    // Set up PAB for LOAD - color data
    pab.OpCode = DSR_LOAD;
    pab.Status = 0;
    pab.VDPBuffer = 0x2000;         // Color table at >2000
    pab.RecordLength = 0;
    pab.CharCount = 0;
    pab.RecordNumber = 6144;        // Max bytes to load
    pab.ScreenOffset = 0;
    pab.NameLength = 0;
    pab.pName = (unsigned char *)filename;

    // Load color data
    result = dsrlnk(&pab, BMP_PAB_ADDR);
    if (result != 0) {
        // Color load failed - use default colors (black on grey)
        vdpmemset(0x2000, 0x1E, 6144);
    }

    // Fill screen with sequential characters to display the image
    // Screen is 32x24, image is 256x192 (32x24 8x8 chars)
    {
        unsigned int row, pos;
        unsigned char ch;

        for (row = 0; row < 24; row++) {
            VDP_SET_ADDRESS_WRITE(gImage + row * 32);
            ch = row * 32;
            for (pos = 0; pos < 32; pos++) {
                VDPWD(ch++);
            }
        }
    }

    // Wait for any key
    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        if (KSCAN_KEY != KSCAN_NOKEY) break;
    }

    // VDP is completely reconfigured - need full restart
    restart_app = 1;
}
