// device.h - TI-99/4A Desktop Environment Device Management (device.c)
#ifndef DEVICE_H
#define DEVICE_H

#include "types.h"

struct PAB;     // from libti99 files.h

// Scan result entry - device name found during CRU scan
typedef struct {
    char name[8];           // Device name (up to 7 chars + null)
} ScanEntry;

// GROM helpers
unsigned int grom_read_word(unsigned int addr, unsigned int port);

// Fill in a PAB structure for a DSR call
void preparePAB(struct PAB *pab, unsigned char opcode, unsigned int address, unsigned int namelen, const char *name);

// Cartridge pseudo-device
unsigned int cart_read_dir(FileEntry *files, unsigned int max_files, unsigned int page);
void cart_setup_ea_environment(void);
void cart_launch_rom(unsigned int entry_addr);
void cart_launch_grom(unsigned int entry_addr, unsigned int port);

// Directory reading
unsigned int device_read_dir(Device *dev, FileEntry *files, unsigned int max_files, unsigned int page);
unsigned int device_read_dir_with_path(Device *dev, const char *path, char *volume_name, FileEntry *files, unsigned int max_files, unsigned int page);

// CRU device scan
unsigned int device_scan_all(void);
ScanEntry *device_get_scan_result(unsigned int idx);
unsigned int device_get_scan_count(void);
unsigned int device_add_from_scan(unsigned int scan_idx);
void device_scan(void);

// Clock (RTC) support
unsigned int clock_read_time(char *time_buf);
void clock_remove(void);
void clock_update_display(void);

#endif // DEVICE_H
