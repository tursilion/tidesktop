// device.c - TI-99/4A Desktop Environment Device Management
#include "config.h"
#include "types.h"
#include "grom.h"
#include "vdp.h"

// Forward declarations
extern void ui_status(const char *msg);
extern void ui_draw_desktop(void);

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

// Check if a DSR exists at the given CRU address
// Returns 1 if found, 0 if not
static unsigned int device_probe_cru(unsigned int cru) {
    // TODO: Implement CRU probing
    // 1. Turn on CRU bit 0 at the address (SBO instruction)
    // 2. Check if ROM appears at >4000
    // 3. Turn off CRU bit (SBZ instruction)
    // 4. Return result

    (void)cru;  // Suppress unused warning for now
    return 0;
}

// Get device name from DSR ROM header
static void device_get_name(unsigned int cru, unsigned int *name) {
    // TODO: Read device name from DSR ROM
    // Located at >4000 area when CRU is activated
    (void)cru;
    *name = ('D' << 8) | 'S';  // Default "DS"
}

// Scan for devices and populate g_app.devices
void device_scan(void) {
    unsigned int i;
    unsigned int dev_idx;
    unsigned int cru;

    // Keep cartridge as device 0
    dev_idx = 1;

    // Scan CRU addresses
    for (i = 0; cru_scan_list[i] != 0 && dev_idx < MAX_DEVICES; i++) {
        cru = cru_scan_list[i];

        if (device_probe_cru(cru)) {
            g_app.devices[dev_idx].cru_base = cru;
            device_get_name(cru, &g_app.devices[dev_idx].name);
            g_app.devices[dev_idx].icon = CHAR_DISK_TL;  // Icon determined by flags
            g_app.devices[dev_idx].flags = DEVICE_DISK;
            dev_idx++;
        }
    }

    g_app.device_count = dev_idx;

    // Redraw desktop with new devices
    ui_draw_desktop();
    ui_status("Scan complete");
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
