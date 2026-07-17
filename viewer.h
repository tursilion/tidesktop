// viewer.h - TI-99/4A Desktop Environment File Viewer (viewer.c)
#ifndef VIEWER_H
#define VIEWER_H

// View a text file onscreen (DIS/VAR or DIS/FIX records)
void viewer_view_file(const char *path, unsigned int is_variable, unsigned int rec_len);

// Bitmap (TI-Artist etc) viewer
unsigned int viewer_is_bitmap(const char *path);
void viewer_show_bitmap(const char *path);

#endif // VIEWER_H
