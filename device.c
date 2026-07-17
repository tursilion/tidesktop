// device.c - TI-99/4A Desktop Environment Device Management
#include "config.h"
#include "types.h"
#include "grom.h"
#include "vdp.h"
#include "kscan.h"
#include "files.h"
#include "string.h"
#include "ui.h"
#include "window.h"
#include "prefs.h"
#include "device.h"

// Scan results buffer
static ScanEntry g_scan_results[MAX_SCAN_DEVICES];
static unsigned int g_scan_count;

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

// PAB addresses for DSR operations
#define CLOCK_PAB_ADDR  0x2800
#define CLOCK_BUF_ADDR  0x2820
#define DIR_PAB_ADDR    0x2880
#define DIR_BUF_ADDR    0x28C0

// shared function to init a PAB
// this loads defaults, you may need other values initialized
// don't call this if you are just updating an existing PAB, old values are blown away!
void preparePAB(struct PAB *pab, unsigned char opcode, unsigned int address, unsigned int namelen, const char *name) {
    memset(pab, 0, sizeof(struct PAB));
    pab->OpCode = opcode;
    pab->VDPBuffer = address;
    pab->NameLength = namelen;
    pab->pName = (unsigned char*)name;
}

// Convert TI radix-100 floating point to integer
// TI format: byte 0 = exponent (biased by 64, radix 100)
//            bytes 1-7 = mantissa (radix 100 digits)
// Negative numbers have bit 7 set in mantissa byte 1
static int ti_float_to_int(unsigned char *fp) {
    int exp;
    int result;
    int negative;
    unsigned char digit;
    int i;

    // Check for zero (exponent byte 0)
    if (fp[0] == 0) return 0;

    // Extract exponent (radix 100, biased by 64)
    exp = (fp[0] & 0x7F) - 64;

    // Check for negative (high bit of mantissa byte 1)
    negative = (fp[1] & 0x80) ? 1 : 0;

    // If exponent is negative, value is < 1
    if (exp < 0) return 0;

    // Build the integer from mantissa digits
    result = 0;
    for (i = 1; i <= 7 && exp >= 0; i++, exp--) {
        digit = fp[i] & 0x7F;  // Remove sign bit if present
        result = result * 100 + digit;
    }

    return negative ? -result : result;
}

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
unsigned int grom_read_word(unsigned int addr, unsigned int port) {
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
// Read a disk directory using DSR
// path: optional subdirectory path (e.g., "SUBDIR." or "DIR1.DIR2.")
// volume_name: optional buffer to receive volume name (12 bytes min, or NULL)
// Returns number of file entries read
static unsigned int disk_read_dir_path(Device *dev, const char *path, char *volume_name, FileEntry *files, unsigned int max_files, unsigned int page) {
    struct PAB pab;
    unsigned char result;
    unsigned char data[256];
    unsigned int count;
    unsigned int record_num;
    unsigned int lfn_mode;
    unsigned int name_len;
    unsigned int i;
    int ftype;
    unsigned int offset;
    char dir_name[PATH_MAX_LEN];  // Device.path (e.g., "DSK1.SUBDIR.")
    unsigned int char_count;
    unsigned int pos;

    if (!dev) return 0;

    // Clear volume name if provided
    if (volume_name) {
        volume_name[0] = 0;
    }

    // Build directory name: "DEV.PATH" (e.g., "DSK1.SUBDIR.")
    pos = 0;
    for (i = 0; i < 7 && dev->name[i]; i++) {
        dir_name[pos++] = dev->name[i];
    }
    dir_name[pos++] = '.';

    // Append path if provided
    if (path) {
        for (i = 0; path[i] && pos < 78; i++) {
            dir_name[pos++] = path[i];
        }
    }
    dir_name[pos] = 0;
    i = pos;  // Set i for pab.NameLength

    // Try opening as Long Filename mode first (Internal Fixed 254)
    preparePAB(&pab, DSR_OPEN, DIR_BUF_ADDR, i, dir_name);
    pab.Status = DSR_TYPE_INTERNAL | DSR_TYPE_INPUT;
    pab.RecordLength = 254;  // LFN mode - but we still need to read back the true value

    //pab.OpCode = DSR_OPEN;
    //pab.VDPBuffer = DIR_BUF_ADDR;
    //pab.CharCount = 0;
    //pab.RecordNumber = 0;
    //pab.ScreenOffset = 0;
    //pab.NameLength = i;
    //pab.pName = (unsigned char *)dir_name;

    result = dsrlnk(&pab, DIR_PAB_ADDR);
    if (result != 0) {
        // LFN failed, try short filename mode (Internal Fixed 38)
        pab.OpCode = DSR_OPEN;
        pab.Status = DSR_TYPE_INTERNAL | DSR_TYPE_INPUT;
        pab.RecordLength = 38;
        pab.RecordNumber = 0;

        result = dsrlnk(&pab, DIR_PAB_ADDR);
        if (result != 0) {
            // Neither mode works - device doesn't support directories
            return 0;
        }
        lfn_mode = 0;
    } else {
        lfn_mode = 1;
        // Read back record length from VDP
        pab.RecordLength = vdpreadchar(DIR_PAB_ADDR + 4);
    }

    // Calculate which records to skip for this page
    record_num = page * max_files;  // after we read the disk index, there's a plus one in the loop to account for it
    count = 0;
    pab.RecordNumber = 0xffff;

    // Read directory entries - always read record zero first to get the disk name!
    while (count < max_files) {
        // Set up PAB for READ
        pab.OpCode = DSR_READ;
        pab.CharCount = 0;
        if (pab.RecordNumber == 0xffff) {
            pab.RecordNumber = 0;
        } else {
            pab.RecordNumber = record_num;
        }

        result = dsrlnk(&pab, DIR_PAB_ADDR);
        if (result != 0) {
            // End of directory or error
            break;
        }

        // Read back CharCount from VDP
        char_count = vdpreadchar(DIR_PAB_ADDR + 5);
        if (char_count == 0 || char_count > sizeof(data)) {
            break;
        }

        // Read data from VDP buffer
        vdpmemread(DIR_BUF_ADDR, data, char_count);

        // Parse the record
        name_len = data[0];
        if (name_len > 32) {
            // our max size!
            record_num++;
            continue;
        }
        if (name_len == 0) {
            break;  // Invalid or end of directory
        }

        // Offset to first float (after name)
        offset = 1 + name_len;

        // Check if we have enough data for the three floats
        // Each float is: 1 byte length + 8 bytes data = 9 bytes, times 3 = 27
        if (offset + 27 > char_count) {
            break;  // Incomplete record
        }

        // Get file type from first float (skip length byte)
        ftype = ti_float_to_int(&data[offset + 1]);

        // Record 0 is disk info - extract volume name, then skip
        if (pab.RecordNumber == 0) {
            // Volume name is in the name field (max 10 chars)
            if (volume_name) {
                unsigned int vol_len = (name_len > 10) ? 10 : name_len;
                for (i = 0; i < vol_len; i++) {
                    volume_name[i] = data[1 + i];
                }
                volume_name[vol_len] = 0;
            }
            record_num++;
            continue;
        }

        // Check for end of directory (type 0)
        if (ftype == 0) {
            break;
        }

        // Copy filename
        for (i = 0; i < name_len && i < 31; i++) {
            files[count].name[i] = data[1 + i];
        }
        files[count].name[i] = 0;

        // Convert TI file type to our type
        // TI: 1=DIS/FIX, 2=DIS/VAR, 3=INT/FIX, 4=INT/VAR, 5=PROGRAM, 6=DIR
        // Negative = protected
        if (ftype < 0) ftype = -ftype;  // Ignore protection for now
        switch (ftype) {
            case 1: files[count].type = FILE_TYPE_DISFIX; break;
            case 2: files[count].type = FILE_TYPE_DISVAR; break;
            case 3: files[count].type = FILE_TYPE_INTFIX; break;
            case 4: files[count].type = FILE_TYPE_INTVAR; break;
            case 5: files[count].type = FILE_TYPE_PROGRAM; break;
            case 6: files[count].type = FILE_TYPE_DIR; break;
            default: files[count].type = FILE_TYPE_DISFIX; break;
        }

        // Get size from second float (offset + 9 for first float, +1 for len byte)
        files[count].size = ti_float_to_int(&data[offset + 10]);

        // Get record length from third float (offset + 18, +1 for len byte)
        files[count].rec_len = ti_float_to_int(&data[offset + 19]);

        count++;
        record_num++;
    }

    // Close the directory
    pab.OpCode = DSR_CLOSE;
    dsrlnk(&pab, DIR_PAB_ADDR);

    return count;
}

unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page) {
    if (!dev) return 0;

    if (dev->flags & DEVICE_CART) {
        return cart_read_dir(files, max_files, page);
    }

    // Read disk directory via DSR (no path, no volume name)
    return disk_read_dir_path(dev, 0, 0, files, max_files, page);
}

// Read directory with subdirectory path and optional volume name output
unsigned int device_read_dir_with_path(Device *dev, const char *path, char *volume_name, FileEntry *files, unsigned int max_files, unsigned int page) {
    if (!dev) return 0;

    if (dev->flags & DEVICE_CART) {
        if (volume_name) volume_name[0] = 0;
        return cart_read_dir(files, max_files, page);
    }

    // Read disk directory via DSR with path
    return disk_read_dir_path(dev, path, volume_name, files, max_files, page);
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

// Check if name matches "CLOCK"
static unsigned int is_clock_device(const char *name) {
    return ((0 == memcmp(name, "CLOCK", 5)) && (name[5] == 0 || name[5] == ' '));
}

// Add a scanned device to the desktop
// Returns 1 if added, 0 if desktop full or special device (CLOCK)
unsigned int device_add_from_scan(unsigned int scan_idx) {
    ScanEntry *scan;
    Device *dev;
    unsigned int i;

    if (scan_idx >= g_scan_count) return 0;

    scan = &g_scan_results[scan_idx];

    // Check for CLOCK device - don't add icon, just set flag
    if (is_clock_device(scan->name)) {
        g_clock_available = 1;
        return 0;  // Don't add as icon
    }

    if (g_app.device_count >= MAX_DEVICES) return 0;

    dev = &g_app.devices[g_app.device_count];

    // Copy name
    for (i = 0; i < 8; i++) {
        dev->name[i] = scan->name[i];
    }

    // Detect device type from name prefix
    if ((0 == memcmp(scan->name, "WDS", 3)) ||
        (0 == memcmp(scan->name, "SCS", 3)) ||
        (0 == memcmp(scan->name, "IDE", 3))) {
        // WDSx, SCS or IDE = hard disk
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
                vdpmemcpy(VDP_SCREEN_POS(row, SEL_WIN_X + 2), "[*]", 3);
            } else {
                vdpmemcpy(VDP_SCREEN_POS(row, SEL_WIN_X + 2), "[ ]", 3);
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
    ui_draw_window(SEL_WIN_X, SEL_WIN_Y, SEL_WIN_W, SEL_WIN_H, "Select Devices");
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
                    // Only count if actually added (CLOCK returns 0)
                    if (device_add_from_scan(i)) {
                        added++;
                    }
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
    unsigned int clock_was_set;

    // Remember if clock was already set before scan
    clock_was_set = g_clock_available;

    count = device_scan_all();

    if (count == 0) {
        ui_status("No devices found");
        return;
    }

    // Run selection dialog
    added = device_run_selection();

    // Persist if the device list changed (or the clock was just found)
    if (added > 0 || (g_clock_available && !clock_was_set)) {
        prefs_save();
    }

    // Redraw desktop with new devices
    ui_draw_desktop();

    // Redraw any active windows (selection dialog may have overlapped)
    window_redraw_all();

    // If clock was newly found, update display immediately
    if (g_clock_available && !clock_was_set) {
        clock_update_display();
    }

    // Show result
    if (added > 0) {
        char msg[20];
        memcpy(msg, "Added ", 6);
        if (added >= 10) {
            msg[6] = '0' + (added / 10);
            msg[7] = '0' + (added % 10);
            msg[8] = 0;
        } else {
            msg[6] = '0' + added;
            msg[7] = 0;
        }
        ui_status(msg);
    } else if (g_clock_available && !clock_was_set) {
        ui_status("Clock enabled");
    } else {
        ui_status("Cancelled");
    }
}

// Set up Editor/Assembler environment before launching a program
// This is the safest environment for most programs
void cart_setup_ea_environment(void) {
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

    // Load standard lowercase character set (a little less standard but nicer)
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

// ============================================================================
// Clock (RTC) Support
// ============================================================================

// Clock device name
static const char clock_name[] = "CLOCK";

// Last displayed time (to avoid redrawing if unchanged)
static char g_last_time[9] = {0};

// Read time from CLOCK device
// Returns 1 if successful, 0 on error
// time_buf should be at least 9 bytes (HH:MM:SS + null)
unsigned int clock_read_time(char *time_buf) {
    struct PAB pab;
    unsigned char result;
    unsigned char data[24];
    unsigned int i;
    unsigned int field;
    unsigned int out_idx;
    unsigned int char_count;

    if (!g_clock_available) return 0;

    // Set up PAB for OPEN
    preparePAB(&pab, DSR_OPEN, CLOCK_BUF_ADDR, 5, clock_name);
    pab.Status = DSR_TYPE_VARIABLE | DSR_TYPE_DISPLAY | DSR_TYPE_UPDATE;

    //pab.OpCode = DSR_OPEN;
    //pab.VDPBuffer = CLOCK_BUF_ADDR;
    //pab.RecordLength = 0;  // Auto-detect
    //pab.CharCount = 0;
    //pab.RecordNumber = 0;
    //pab.ScreenOffset = 0;
    //pab.NameLength = 5;
    //pab.pName = (unsigned char *)clock_name;

    // Open the device
    result = dsrlnk(&pab, CLOCK_PAB_ADDR);
    if (result != 0) {
        return 0;
    }

    // Read back the record length from VDP (DSR updates it)
    pab.RecordLength = vdpreadchar(CLOCK_PAB_ADDR + 4);

    // Set up PAB for READ
    pab.OpCode = DSR_READ;
    pab.CharCount = 0;

    // Read the record
    result = dsrlnk(&pab, CLOCK_PAB_ADDR);
    if (result != 0) {
        // Close and return error
        pab.OpCode = DSR_CLOSE;
        dsrlnk(&pab, CLOCK_PAB_ADDR);
        return 0;
    }

    // Read back CharCount from VDP (DSR updates it)
    char_count = vdpreadchar(CLOCK_PAB_ADDR + 5);
    if (char_count > 23) char_count = 23;

    // Read data from VDP buffer
    vdpmemread(CLOCK_BUF_ADDR, data, char_count);
    data[char_count] = 0;

    // Close the device
    pab.OpCode = DSR_CLOSE;
    dsrlnk(&pab, CLOCK_PAB_ADDR);

    // Parse the data - format is "dow,date,time"
    // We want the third field (time)
    field = 0;
    out_idx = 0;
    for (i = 0; data[i] && out_idx < 8; i++) {
        if (data[i] == ',') {
            field++;
        } else if (field == 2) {
            // We're in the time field
            time_buf[out_idx++] = data[i];
        }
    }
    time_buf[out_idx] = 0;

    return (out_idx > 0) ? 1 : 0;
}

// Remove the clock device - forget it and clear the time display
void clock_remove(void) {
    unsigned int i;

    g_clock_available = 0;

    // Forget last displayed time so a future re-add redraws
    for (i = 0; i < 9; i++) {
        g_last_time[i] = 0;
    }

    // Restore the divider chars where the time was displayed
    hchar(SCREEN_HEIGHT - 2, SCREEN_WIDTH - 9, CHAR_DIVIDER, 8);
}

// Update the clock display on screen
// Called periodically from main loop
void clock_update_display(void) {
    char time_buf[9];
    unsigned int i;
    unsigned int col;
    unsigned int changed;

    if (!g_clock_available) return;

    // Read current time
    if (!clock_read_time(time_buf)) return;

    // Check if time changed
    changed = 0;
    for (i = 0; i < 8; i++) {
        if (time_buf[i] != g_last_time[i]) {
            changed = 1;
            g_last_time[i] = time_buf[i];
        }
        if (time_buf[i] == 0) break;
    }

    if (!changed) return;

    // Display time on bottom separator (row 22, right side)
    // Time is up to 8 chars (HH:MM:SS), display at right edge
    col = SCREEN_WIDTH - 9;  // Leave 1 char margin
    for (i = 0; time_buf[i] && i < 8; i++) {
        vdpscreenchar(VDP_SCREEN_POS(SCREEN_HEIGHT - 2, col + i), time_buf[i]);
    }
}
