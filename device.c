// device.c - TI-99/4A Desktop Environment Device Management
#include "config.h"
#include "types.h"
#include "grom.h"
#include "vdp.h"
#include "kscan.h"

// Forward declarations
extern void ui_status(const char *msg);
extern void ui_draw_desktop(void);

// Scan result entry - device name found during CRU scan
typedef struct {
    char name[8];           // Device name (up to 7 chars + null)
    unsigned int cru_base;  // CRU base where found
} ScanEntry;

// Scan results buffer
static ScanEntry g_scan_results[MAX_SCAN_DEVICES];
static unsigned int g_scan_count;

// External character set loader from libti99
extern void charsetlc(void);

// CRU base addresses to scan for disk controllers
// Standard TI floppy: >1100
// Common others: >1000, >1200, >1300, etc.
static const unsigned int cru_scan_list[] = {
    0x1000,  // Alternate
    0x1100,  // TI Disk Controller
    0x1200,  // Myarc HFDC, etc.
    0x1300,  // Additional controllers
    0x1400,
    0x1500,
    0x1600,
    0x1700,
    0x1800,
    0x1900,
    0x1a00,
    0x1b00,
    0x1c00,
    0x1d00,
    0x1e00,
    0x1f00,
    0
};

// GROM addresses to scan for cartridge headers
// Console GROMs first (always present), then cartridge area
// We omit 0000 for two reasons. First, there's no programs there.
// second, 0 is the end of list terminator.
static const unsigned int grom_scan_addrs[] = {
    0x2000,  // Console GROM 1
    0x4000,  // Console GROM 2
    0x6000,  // Cartridge GROM 0
    0x8000,  // Cartridge GROM 1
    0xA000,  // Cartridge GROM 2
    0xC000,  // Cartridge GROM 3
    0xE000,  // Cartridge GROM 4
    0       // End marker
};

// Header flag indicating valid cartridge header
#define CART_HEADER_FLAG    0xAA

// Read a 16-bit big-endian value from GROM
static unsigned int grom_read_word(unsigned int addr, unsigned int port) {
    unsigned int hi, lo;
    hi = GromReadData(addr, port);
    lo = GromReadData(addr + 1, port);
    return (hi << 8) | lo;
}

// Read a 16-bit big-endian value from ROM at CPU address
static unsigned int rom_read_word(unsigned int addr) {
    volatile unsigned int *p = (volatile unsigned int *)addr;
    return *p;
}

// Add a GROM program to the file list
// Returns 1 if added, 0 if skipped (before start_idx) or full
static unsigned int cart_add_grom_program(
    FileEntry *files,
    unsigned int *file_count,
    unsigned int max_files,
    unsigned int *current_idx,
    unsigned int start_idx,
    unsigned int entry_addr,
    unsigned int port
) {
    unsigned int name_len;
    unsigned int i;
    FileEntry *file;

    // Skip entries before our starting index
    if (*current_idx < start_idx) {
        (*current_idx)++;
        return 0;
    }

    // Check if we have room
    if (*file_count >= max_files) {
        return 0;
    }

    file = &files[*file_count];

    // Entry point is at offset 2-3
    file->size = grom_read_word(entry_addr + 2, port);
    file->type = FILE_TYPE_GROM;
    file->rec_len = port;

    // Name length at offset 4
    name_len = GromReadData(entry_addr + 4, port);
    if (name_len > 31) name_len = 31;

    // Read name starting at offset 5
    for (i = 0; i < name_len; i++) {
        file->name[i] = GromReadData(entry_addr + 5 + i, port);
    }
    file->name[name_len] = 0;

    (*file_count)++;
    (*current_idx)++;

    return 1;
}

// Add a ROM program to the file list
static unsigned int cart_add_rom_program(
    FileEntry *files,
    unsigned int *file_count,
    unsigned int max_files,
    unsigned int *current_idx,
    unsigned int start_idx,
    unsigned int entry_addr
) {
    unsigned int name_len;
    unsigned int i;
    FileEntry *file;
    volatile unsigned int *pw = (volatile unsigned int *)entry_addr;
    volatile unsigned char *pb = (volatile unsigned char *)entry_addr;

    // Skip entries before our starting index
    if (*current_idx < start_idx) {
        (*current_idx)++;
        return 0;
    }

    // Check if we have room
    if (*file_count >= max_files) {
        return 0;
    }

    file = &files[*file_count];

    // Entry point is at offset 2-3 (word 1)
    file->size = pw[1];
    file->type = FILE_TYPE_ROM;
    file->rec_len = 0;

    // Name length at offset 4
    name_len = pb[4];
    if (name_len > 31) name_len = 31;

    // Read name starting at offset 5
    for (i = 0; i < name_len; i++) {
        file->name[i] = pb[5 + i];
    }
    file->name[name_len] = 0;

    (*file_count)++;
    (*current_idx)++;

    return 1;
}

// Scan GROM programs at a specific address and port
// Programs are stored in reverse order, so we need to walk twice
static void cart_scan_grom_list(
    FileEntry *files,
    unsigned int *file_count,
    unsigned int max_files,
    unsigned int *current_idx,
    unsigned int start_idx,
    unsigned int list_addr,
    unsigned int port
) {
    unsigned int count;
    unsigned int i;
    unsigned int addr;
    unsigned int link;
    unsigned int addr_buf[32];  // Max 32 programs per list

    // First pass: count and collect addresses
    count = 0;
    addr = list_addr;
    while (addr != 0 && count < 32) {
        addr_buf[count] = addr;
        link = grom_read_word(addr, port);
        count++;
        addr = link;
    }

    // Second pass: add in reverse order
    for (i = count; i > 0; i--) {
        if (*file_count >= max_files) break;
        cart_add_grom_program(files, file_count, max_files, current_idx, start_idx,
                              addr_buf[i - 1], port);
    }
}

// Scan ROM programs at CPU >6000
static void cart_scan_rom_list(
    FileEntry *files,
    unsigned int *file_count,
    unsigned int max_files,
    unsigned int *current_idx,
    unsigned int start_idx,
    unsigned int list_addr
) {
    unsigned int count;
    unsigned int i;
    unsigned int addr;
    unsigned int addr_buf[32];
    volatile unsigned int *pw;

    // First pass: count and collect addresses
    count = 0;
    addr = list_addr;
    while (addr != 0 && count < 32) {
        addr_buf[count] = addr;
        pw = (volatile unsigned int *)addr;
        addr = pw[0];  // Link is first word
        count++;
    }

    // Second pass: add in reverse order
    for (i = count; i > 0; i--) {
        if (*file_count >= max_files) break;
        cart_add_rom_program(files, file_count, max_files, current_idx, start_idx,
                             addr_buf[i - 1]);
    }
}

// Check if GROM has a valid header at the given address
static unsigned int cart_check_grom_header(unsigned int addr, unsigned int port) {
    return (GromReadData(addr, port) == CART_HEADER_FLAG);
}

// Get program list pointer from GROM header
static unsigned int cart_get_grom_list(unsigned int addr, unsigned int port) {
    return grom_read_word(addr + 6, port);
}

// Check if cartridge has multiple GROM bases
// Compare first 16 bytes at port 0 vs port 1 at >6000
static unsigned int cart_has_bases(void) {
    unsigned int i;
    unsigned int b0, b1;

    for (i = 0; i < 16; i++) {
        b0 = GromReadData(0x6000 + i, 0);
        b1 = GromReadData(0x6000 + i, 1);
        if (b0 != b1) {
            return 1;  // Bases differ
        }
    }

    return 0;  // Same data, no bases
}

// Scan GROM addresses at a specific port
// Returns updated file_count
static unsigned int cart_scan_port(
    FileEntry *files,
    unsigned int file_count,
    unsigned int max_files,
    unsigned int *current_idx,
    unsigned int start_idx,
    unsigned int port,
    unsigned int start_addr_idx
) {
    unsigned int i;
    unsigned int addr, list_addr;
    
    //__asm volatile( "data 0x0113" );    // classic99 breakpoint

    for (i = start_addr_idx; grom_scan_addrs[i] != 0; i++) {
        addr = grom_scan_addrs[i];
        if (cart_check_grom_header(addr, port)) {
            list_addr = cart_get_grom_list(addr, port);
            if (list_addr != 0) {
                cart_scan_grom_list(files, &file_count, max_files, current_idx,
                                    start_idx, list_addr, port);
                if (file_count >= max_files) {
                    return file_count;
                }
            }
        }
    }

    return file_count;
}

// Scan cartridge and fill file list
// page: 0-based page number (each page = WIN_MAX_FILES entries)
// Returns number of entries added
unsigned int cart_read_dir(FileEntry *files, unsigned int max_files, unsigned int page) {
    unsigned int file_count = 0;
    unsigned int current_idx = 0;
    unsigned int start_idx = page * max_files;
    unsigned int port;
    unsigned int list_addr;
    unsigned int has_bases;

    // First: scan ALL GROM addresses at port 0 (console + cartridge)
    file_count = cart_scan_port(files, file_count, max_files, &current_idx,
                                start_idx, 0, 0);
    if (file_count >= max_files) {
        return file_count;
    }

    // Check for multiple GROM bases
    has_bases = cart_has_bases();

    if (has_bases) {
        // Scan cartridge GROM addresses at ports 1-15
        // Start at index 3 (>6000) since console GROMs don't have bases
        for (port = 1; port < 16; port++) {
            file_count = cart_scan_port(files, file_count, max_files, &current_idx,
                                        start_idx, port, 2);    // start at >6000, entry 2
            if (file_count >= max_files) {
                return file_count;
            }
        }
    }

    // Check ROM at CPU >6000
    {
        volatile unsigned int *rom_hdr = (volatile unsigned int *)0x6000;
        if ((rom_hdr[0] >> 8) == CART_HEADER_FLAG) {
            list_addr = rom_hdr[3];  // Offset 6-7 is word 3
            if (list_addr != 0) {
                cart_scan_rom_list(files, &file_count, max_files, &current_idx,
                                   start_idx, list_addr);
            }
        }
    }

    return file_count;
}

// Read directory for a device
// dev: device to read
// files: array to fill
// max_files: maximum entries to return
// page: 0-based page number
// Returns number of entries added
unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page) {
    if (!dev) return 0;

    if (dev->flags & DEVICE_CART) {
        return cart_read_dir(files, max_files, page);
    }

    // TODO: Implement disk directory reading
    // For now, return 0 entries
    return 0;
}

// Enable a DSR card at CRU base
static void cru_enable(unsigned int cru) {
    __asm__ volatile (
        "mov %0, r12\n\t"
        "sbo 0"
        :
        : "r" (cru)
        : "r12"
    );
}

// Disable a DSR card at CRU base
static void cru_disable(unsigned int cru) {
    __asm__ volatile (
        "mov %0, r12\n\t"
        "sbz 0"
        :
        : "r" (cru)
        : "r12"
    );
}

// Scan device names at a CRU address
// Returns number of devices found at this address
static unsigned int scan_cru_devices(unsigned int cru) {
    volatile unsigned char *rom = (volatile unsigned char *)0x4000;
    unsigned int list_addr;
    unsigned int link;
    unsigned char name_len;
    unsigned int i;
    unsigned int count = 0;
    ScanEntry *entry;

    // Enable the card
    cru_enable(cru);

    // Check for valid DSR header (0xAA at >4000)
    if (rom[0] != 0xAA) {
        cru_disable(cru);
        return 0;
    }

    // Device name list pointer is at >4008-4009 (big endian)
    list_addr = (rom[8] << 8) | rom[9];

    // Walk the device name list
    while (list_addr != 0 && g_scan_count < MAX_SCAN_DEVICES) {
        volatile unsigned char *entry_ptr = (volatile unsigned char *)list_addr;

        // Link to next entry (big endian word at offset 0)
        link = (entry_ptr[0] << 8) | entry_ptr[1];

        // Skip offset 2-3 (DSR entry point)

        // Name length at offset 4
        name_len = entry_ptr[4];
        if (name_len > 7) name_len = 7;

        // Copy name (starting at offset 5)
        entry = &g_scan_results[g_scan_count];
        for (i = 0; i < name_len; i++) {
            entry->name[i] = entry_ptr[5 + i];
        }
        entry->name[name_len] = 0;
        entry->cru_base = cru;

        g_scan_count++;
        count++;

        // Move to next entry
        list_addr = link;
    }

    // Disable the card
    cru_disable(cru);

    return count;
}

// Scan for all devices across all CRU addresses
// Returns total number of devices found
unsigned int device_scan_all(void) {
    unsigned int i;

    // Clear previous scan results
    g_scan_count = 0;

    // Show scanning status
    ui_status("Scanning...");

    // Scan all CRU addresses
    for (i = 0; cru_scan_list[i] != 0 && g_scan_count < MAX_SCAN_DEVICES; i++) {
        scan_cru_devices(cru_scan_list[i]);
    }

    return g_scan_count;
}

// Get scan result by index
ScanEntry *device_get_scan_result(unsigned int idx) {
    if (idx < g_scan_count) {
        return &g_scan_results[idx];
    }
    return 0;
}

// Get total scan count
unsigned int device_get_scan_count(void) {
    return g_scan_count;
}

// Add a scanned device to the desktop
// Returns 1 if added, 0 if desktop full
unsigned int device_add_from_scan(unsigned int scan_idx) {
    ScanEntry *scan;
    Device *dev;
    unsigned int i;

    if (scan_idx >= g_scan_count) return 0;
    if (g_app.device_count >= MAX_DEVICES) return 0;

    scan = &g_scan_results[scan_idx];
    dev = &g_app.devices[g_app.device_count];

    // Copy name
    for (i = 0; i < 8; i++) {
        dev->name[i] = scan->name[i];
    }
    dev->cru_base = scan->cru_base;

    // Detect device type from name prefix
    if (scan->name[0] == 'W' && scan->name[1] == 'D' && scan->name[2] == 'S') {
        // WDSx = hard disk
        dev->icon = CHAR_HD_TL;
        dev->flags = DEVICE_HD;
    } else {
        // DSKx and others = floppy disk
        dev->icon = CHAR_DISK_TL;
        dev->flags = DEVICE_DISK;
    }

    g_app.device_count++;
    return 1;
}

// Selection state for device picker
static unsigned int g_sel_bits[4];  // 64 bits for selection state
#define SEL_WIN_X       6
#define SEL_WIN_Y       3
#define SEL_WIN_W       20
#define SEL_WIN_H       16
#define SEL_VISIBLE     14  // Visible rows in selection window

// Check if device at index is selected
static unsigned int device_is_selected(unsigned int idx) {
    if (idx >= 64) return 0;
    return (g_sel_bits[idx >> 4] >> (idx & 15)) & 1;
}

// Toggle selection at index
static void device_toggle_selected(unsigned int idx) {
    if (idx >= 64) return;
    g_sel_bits[idx >> 4] ^= (1 << (idx & 15));
}

// Draw the selection window content
static void device_draw_sel_content(unsigned int scroll, unsigned int cursor) {
    unsigned int i;
    unsigned int row;
    unsigned int visible;
    ScanEntry *entry;

    visible = (g_scan_count < SEL_VISIBLE) ? g_scan_count : SEL_VISIBLE;

    for (i = 0; i < SEL_VISIBLE; i++) {
        row = SEL_WIN_Y + 1 + i;
        unsigned int idx = scroll + i;

        // Clear line
        hchar(row, SEL_WIN_X + 1, ' ', SEL_WIN_W - 2);

        if (idx < g_scan_count) {
            entry = &g_scan_results[idx];

            // Cursor indicator
            if (idx == cursor) {
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 1), '>');
            }

            // Selection checkbox
            if (device_is_selected(idx)) {
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 2), '[');
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 3), '*');
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 4), ']');
            } else {
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 2), '[');
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 3), ' ');
                vdpscreenchar(VDP_SCREEN_POS(row, SEL_WIN_X + 4), ']');
            }

            // Device name
            {
                unsigned int addr = gImage + VDP_SCREEN_POS(row, SEL_WIN_X + 6);
                unsigned int j;
                VDP_SET_ADDRESS_WRITE(addr);
                for (j = 0; j < 7 && entry->name[j]; j++) {
                    VDPWD(entry->name[j]);
                }
            }
        }
    }
}

// Draw the selection window frame
static void device_draw_sel_window(void) {
    unsigned int i;

    // Top border
    vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y, SEL_WIN_X), CHAR_WIN_TL);
    hchar(SEL_WIN_Y, SEL_WIN_X + 1, CHAR_WIN_H, SEL_WIN_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y, SEL_WIN_X + SEL_WIN_W - 1), CHAR_WIN_TR);

    // Title
    {
        unsigned int addr = gImage + VDP_SCREEN_POS(SEL_WIN_Y, SEL_WIN_X + 2);
        VDP_SET_ADDRESS_WRITE(addr);
        VDPWD('S'); VDPWD('e'); VDPWD('l'); VDPWD('e'); VDPWD('c'); VDPWD('t');
        VDPWD(' '); VDPWD('D'); VDPWD('e'); VDPWD('v'); VDPWD('i'); VDPWD('c');
        VDPWD('e'); VDPWD('s');
    }

    // Side borders and clear interior
    for (i = 1; i < SEL_WIN_H - 1; i++) {
        vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y + i, SEL_WIN_X), CHAR_WIN_V);
        hchar(SEL_WIN_Y + i, SEL_WIN_X + 1, ' ', SEL_WIN_W - 2);
        vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y + i, SEL_WIN_X + SEL_WIN_W - 1), CHAR_WIN_V);
    }

    // Bottom border
    vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y + SEL_WIN_H - 1, SEL_WIN_X), CHAR_WIN_BL);
    hchar(SEL_WIN_Y + SEL_WIN_H - 1, SEL_WIN_X + 1, CHAR_WIN_H, SEL_WIN_W - 2);
    vdpscreenchar(VDP_SCREEN_POS(SEL_WIN_Y + SEL_WIN_H - 1, SEL_WIN_X + SEL_WIN_W - 1), CHAR_WIN_BR);
}

// Run the device selection dialog
// Returns number of devices added
static unsigned int device_run_selection(void) {
    unsigned int cursor = 0;
    unsigned int scroll = 0;
    unsigned int key;
    unsigned int added = 0;
    unsigned int i;

    // Clear selection bits
    for (i = 0; i < 4; i++) {
        g_sel_bits[i] = 0;
    }

    // Draw window
    device_draw_sel_window();
    device_draw_sel_content(scroll, cursor);
    ui_status("Space:Sel  Enter:Add  Fctn-9:Cancel");

    // Input loop
    for (;;) {
        kscan(KSCAN_MODE_BASIC);
        key = KSCAN_KEY;

        if (key == KSCAN_NOKEY) continue;

        // Small delay for debounce
        {
            volatile unsigned int d;
            for (d = 0; d < 1000; d++);
        }

        // Wait for key release
        while (KSCAN_KEY != KSCAN_NOKEY) {
            kscan(KSCAN_MODE_BASIC);
        }

        if (key == KEY_BACK) {
            // Cancel - no devices added
            break;
        }

        if (key == KEY_ENTER) {
            // Add all selected devices
            for (i = 0; i < g_scan_count && g_app.device_count < MAX_DEVICES; i++) {
                if (device_is_selected(i)) {
                    device_add_from_scan(i);
                    added++;
                }
            }
            break;
        }

        if (key == KEY_UP && cursor > 0) {
            cursor--;
            if (cursor < scroll) {
                scroll = cursor;
            }
            device_draw_sel_content(scroll, cursor);
        }

        if (key == KEY_DOWN && cursor + 1 < g_scan_count) {
            cursor++;
            if (cursor >= scroll + SEL_VISIBLE) {
                scroll = cursor - SEL_VISIBLE + 1;
            }
            device_draw_sel_content(scroll, cursor);
        }

        if (key == ' ') {
            // Toggle selection
            device_toggle_selected(cursor);
            device_draw_sel_content(scroll, cursor);
        }

        if (key == KEY_PROCEED) {
            // Page up
            if (cursor >= SEL_VISIBLE) {
                cursor -= SEL_VISIBLE;
                scroll = cursor;
            } else {
                cursor = 0;
                scroll = 0;
            }
            device_draw_sel_content(scroll, cursor);
        }

        if (key == KEY_CLEAR) {
            // Page down
            if (cursor + SEL_VISIBLE < g_scan_count) {
                cursor += SEL_VISIBLE;
                scroll = cursor;
                // Don't scroll past last page
                if (scroll + SEL_VISIBLE > g_scan_count) {
                    scroll = (g_scan_count > SEL_VISIBLE) ? g_scan_count - SEL_VISIBLE : 0;
                }
            } else {
                cursor = g_scan_count - 1;
            }
            device_draw_sel_content(scroll, cursor);
        }
    }

    return added;
}

// Main scan function - scans and opens selection window
void device_scan(void) {
    unsigned int count;
    unsigned int added;

    count = device_scan_all();

    if (count == 0) {
        ui_status("No devices found");
        return;
    }

    // Run selection dialog
    added = device_run_selection();

    // Redraw desktop with new devices
    ui_draw_desktop();

    // Show result
    if (added > 0) {
        char msg[20];
        msg[0] = 'A'; msg[1] = 'd'; msg[2] = 'd'; msg[3] = 'e';
        msg[4] = 'd'; msg[5] = ' ';
        if (added >= 10) {
            msg[6] = '0' + (added / 10);
            msg[7] = '0' + (added % 10);
            msg[8] = 0;
        } else {
            msg[6] = '0' + added;
            msg[7] = 0;
        }
        ui_status(msg);
    } else {
        ui_status("Cancelled");
    }
}

// Open a device and read its directory
unsigned int device_open(Device *dev) {
    // TODO: Implement directory reading
    // For disk: Open device directory (do not use direct sector reads). Ask user for details on LFN extension.
    // For cartridge: Enumerate ROM program headers

    (void)dev;
    return 0;
}

// Set up Editor/Assembler environment before launching a program
// This is the safest environment for most programs
static void cart_setup_ea_environment(void) {
    // Set VDP registers 0-7 to E/A defaults
    // R0=00, R1=E0, R2=00, R3=0E, R4=01, R5=06, R6=00, R7=F3
    VDP_SET_REGISTER(VDP_REG_MODE1, 0x80);   // 16K, display off, int disabled
    VDP_SET_REGISTER(VDP_REG_MODE0, 0x00);   // No external video
    VDP_SET_REGISTER(VDP_REG_SIT,   0x00);   // Screen image table at >0000
    VDP_SET_REGISTER(VDP_REG_CT,    0x0E);   // Color table at >0380
    VDP_SET_REGISTER(VDP_REG_PDT,   0x01);   // Pattern table at >0800
    VDP_SET_REGISTER(VDP_REG_SAL,   0x06);   // Sprite attr table at >0300
    VDP_SET_REGISTER(VDP_REG_SDT,   0x00);   // Sprite pattern table at >0000
    VDP_SET_REGISTER(VDP_REG_COL,   0xF3);   // White on light green

    // Clear screen (768 bytes of spaces at >0000)
    vdpmemset(0x0000, 0x20, 768);

    // Set all colors to black on transparent (32 bytes at >0380)
    vdpmemset(0x0380, 0x10, 32);

    // turn off sprites
    vdpchar(0x300, 0xd0);

    // Set pattern table pointer for character loader
    gPattern = 0x0800;

    // Load standard lowercase character set
    charsetlc();

    VDP_SET_REGISTER(VDP_REG_MODE1, 0xE0);   // 16K, display on, int enabled
}

// Launch a ROM cartridge program
// entry_addr: CPU address to branch to (from program list)
// This function does not return
void cart_launch_rom(unsigned int entry_addr) {
    // Set up E/A environment first
    cart_setup_ea_environment();

    // Branch to the entry point
    // The ROM program takes over from here
    __asm__ volatile (
        "mov %0, r0\n\t"
        "b *r0"
        :
        : "r" (entry_addr)
        : "r0"
    );
    // Never reached
    for (;;);
}

// Launch a GROM cartridge program via GPL interpreter
// entry_addr: GROM address to start at (from program list)
// port: GROM port (0-15)
// This function does not return
void cart_launch_grom(unsigned int entry_addr, unsigned int port) {
    volatile unsigned int *grom_base = (volatile unsigned int *)0x83FA;
    volatile unsigned int *gpl_start = (volatile unsigned int *)0x83EC;

    // Set up E/A environment first
    cart_setup_ea_environment();

    // Set GROM base address: (port * 4) + 0x9800
    *grom_base = (port << 2) + 0x9800;

    // Set GPL start address
    *gpl_start = entry_addr;

    // Switch to GPL workspace and branch to interpreter
    __asm__ volatile (
        "lwpi >83E0\n\t"
        "b @>0060"
    );
    // Never reached
    for (;;);
}
