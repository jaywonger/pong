#ifndef __VGA_STUFF__

void wait_for_vsync();

void clear_screen(int back_buffer);

void LCD_clear(void);

void drawCircle(int xc, int yc, int x, int y, int color);

void circleBres(int xc, int yc, int r, int color);

void draw_line(int x0, int y0, int x1, int y1, int color, int backbuffer);

void VGA_text(int x, int y, char * text_ptr);

void VGA_clear();

void VGA_pixel(int x, int y, short pixel_color);

#endif
