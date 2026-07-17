// input.h - TI-99/4A Desktop Environment Input Handling (input.c)
#ifndef INPUT_H
#define INPUT_H

// Main input polling loop body - call once per frame
void input_process(void);

// Restore selection display after restart (called from main.c)
void input_restore_selection(void);

#endif // INPUT_H
