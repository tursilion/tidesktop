// test.c - Simple test to verify files.h DSR interface
#include "vdp.h"
#include "conio.h"
#include "files.h"

#define TEST_PAB_ADDR 0x3000

unsigned char result;

int main(void) {
    struct PAB pab;

    // Set up text mode
    set_text();
    clrscr();

    cputs("DSR Open Test\n\n");
    cputs("Opening DSK1.NOFILE...\n");

    // Set up PAB for OPEN on a non-existent file
    pab.OpCode = DSR_OPEN;
    pab.Status = DSR_TYPE_DISPLAY | DSR_TYPE_INPUT | DSR_TYPE_SEQUENTIAL;
    pab.VDPBuffer = TEST_PAB_ADDR + 64;
    pab.RecordLength = 80;
    pab.CharCount = 0;
    pab.RecordNumber = 0;
    pab.ScreenOffset = 0;
    pab.NameLength = 0;  // auto-detect from string
    pab.pName = (unsigned char*)"DSK1.NOFILE";

    // Try to open the file
    result = dsrlnk(&pab, TEST_PAB_ADDR);

    cputs("Result: ");
    if (result == 0) {
        cputs("0 (Success?!)\n");
    } else {
        // Print hex result
        cprintf("0x%x\n", result);

        // Decode error
        cputs("Error: ");
        switch (GET_ERROR(result)) {
            case DSR_ERR_NONE:
                cputs("None\n");
                break;
            case DSR_ERR_WRITEPROTECT:
                cputs("Write protected\n");
                break;
            case DSR_ERR_BADATTRIBUTE:
                cputs("Bad attribute\n");
                break;
            case DSR_ERR_ILLEGALOPCODE:
                cputs("Illegal opcode\n");
                break;
            case DSR_ERR_MEMORYFULL:
                cputs("Memory full\n");
                break;
            case DSR_ERR_PASTEOF:
                cputs("Past EOF\n");
                break;
            case DSR_ERR_DEVICEERROR:
                cputs("Device error\n");
                break;
            case DSR_ERR_FILEERROR:
                cputs("File error\n");
                break;
            default:
                if (result == DSR_ERR_DSRNOTFOUND) {
                    cputs("DSR not found\n");
                } else {
                    cputs("Unknown\n");
                }
                break;
        }
    }

    cputs("\nPress any key...");
    cgetc();

    return 0;
}
