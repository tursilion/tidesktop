// device.c - TI-99/4A Desktop Environment Device Management
#include "config.h"
#include "types.h"

// Forward declarations
extern void ui_status(const char *msg);
extern void ui_draw_desktop(void);

// CRU base addresses to scan for disk controllers
// Standard TI floppy: >1100
// Common others: >1000, >1200, >1300, etc.
static const unsigned int cru_scan_list[] = {
    0x1100,  // TI Disk Controller
    0x1000,  // Alternate
    0x1200,  // Myarc HFDC, etc.
    0x1300,  // Additional controllers
    0x1400,
    0x1500,
    0x1600,
    0
};

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
