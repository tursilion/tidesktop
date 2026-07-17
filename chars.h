// chars.h - TI-99/4A Desktop Environment Character Definitions (chars.c)
#ifndef CHARS_H
#define CHARS_H

// Load all custom character patterns and colors into the VDP
void chars_init(void);

// Convert ASCII to title bar mini-font character
unsigned int char_to_title(unsigned int c);

#endif // CHARS_H
