// USEFUL Functions
#include "math.h"

/* Screen size. */
#define RESOLUTION_X	320
#define RESOLUTION_Y	240
#define ABS(x)			(((x) > 0) ? (x) : -(x))

/* VGA Control registers. */
volatile int *vga_pixel_buffer_buffer_reg = (int *) 0x10003020;
volatile int *vga_pixel_buffer_back_buffer_reg = (int *) 0x10003024;
volatile int *vga_pixel_buffer_status_reg = (int *) 0x1000302C;
volatile char * LCD_display_ptr = (char *) 0xFF203050;	// 16x2 character display
volatile short * pixel_buffer = (short *) 0x08000000;

/* Video memory */
volatile int *vga_screen_front_buffer = (int *) 0x08000000;
volatile int *vga_screen_back_buffer = (int *) 0x08040000;

volatile int * JTAG_UART_ptr 	= (int *) 0x10001000;	// JTAG UART address

/* Character buffer */
volatile char * character_buffer = (char *) 0x09000000; // VGA character buffer


/******************************************************************************/
/**** VGA SYNCHRONIZATION FUNCTIONS *******************************************/
/******************************************************************************/

void wait_for_vsync()
{
	register int status;
	// Wait for vertical synchronization.
	*vga_pixel_buffer_buffer_reg = 1;
	status = *vga_pixel_buffer_status_reg;
	while( (status & 0x01) != 0 )
	{
		status = *vga_pixel_buffer_status_reg;
	}
}

/******************************************************************************/
/**** LINE DRAWING AND CLEAR SCREEN FUNCTIONS *********************************/
/******************************************************************************/

void clear_screen(int back_buffer)
{
	register int *buffer = (back_buffer != 0) ? vga_screen_back_buffer : vga_screen_front_buffer;
	register int y,x;
	for (y = 0; y < RESOLUTION_Y; y++)
	{
		register int *buf = (buffer + (y << 8));
		for (x = 0; x < RESOLUTION_X/2; x++)
		{
			*buf++ = 0;
		}
	}
}

void LCD_clear(void)
{
	*(LCD_display_ptr) = 0x01;				// clear the LCD
}

void helper_plot_pixel(int buffer_start, int x, int y, short int line_color)
{
	*((short int *)(buffer_start + (y << 10) + (x << 1))) = (short int) line_color;
}

// Function to put pixels
// at subsequence points
void drawCircle(int xc, int yc, int x, int y, int color)
{
	register unsigned int buffer_start;
	register int circle_color = color;

	//buffer_start = (int) vga_screen_back_buffer;
	buffer_start = (int) vga_screen_front_buffer;

	helper_plot_pixel(buffer_start, xc+x, yc+y, circle_color);
	helper_plot_pixel(buffer_start, xc-x, yc+y, circle_color);
	helper_plot_pixel(buffer_start, xc+x, yc-y, circle_color);
	helper_plot_pixel(buffer_start, xc-x, yc-y, circle_color);
	helper_plot_pixel(buffer_start, xc+x, yc+y, circle_color);
	helper_plot_pixel(buffer_start, xc-x, yc+y, circle_color);
	helper_plot_pixel(buffer_start, xc+x, yc-y, circle_color);
	helper_plot_pixel(buffer_start, xc-x, yc-y, circle_color);
	helper_plot_pixel(buffer_start, xc, yc, circle_color);
	helper_plot_pixel(buffer_start, xc, yc+y, circle_color);
	helper_plot_pixel(buffer_start, xc, yc-y, circle_color);
	helper_plot_pixel(buffer_start, xc+x, yc, circle_color);
	helper_plot_pixel(buffer_start, xc-x, yc, circle_color);
}

// Function for circle-generation
// using Bresenham's algorithm
void circleBres(int xc, int yc, int r, int color)
{
	register int circle_color = color;
    int x = 0, y = r;
    int d = 3 - 2 * r;

    while (y >= x)
    {
        // for each pixel we will
        // draw all eight pixels
        drawCircle(xc, yc, x, y, circle_color);
        x++;

        // check for decision parameter
        // and correspondingly
        // update d, x, y
        if (d > 0)
        {
            y--;
            d = d + 4 * (x - y) + 10;
        }
        else
            d = d + 4 * x + 6;
        drawCircle(xc, yc, x, y, circle_color);
        //delay(50);
    }
}

/* Bresenham's line drawing algorithm. */
void draw_line(int x0, int y0, int x1, int y1, int color, int backbuffer)
/* This function draws a line between points (x0, y0) and (x1, y1). The function does not check if it draws a pixel within screen boundaries.
 * users should ensure that the line is drawn within the screen boundaries. */
{
	register int x_0 = x0;
	register int y_0 = y0;
	register int x_1 = x1;
	register int y_1 = y1;
	register char steep = (ABS(y_1 - y_0) > ABS(x_1 - x_0)) ? 1 : 0;
	register int deltax, deltay, error, ystep, x, y;
	register int line_color = color;
	register unsigned int buffer_start;

	if (backbuffer == 1)
		buffer_start = (int) vga_screen_back_buffer;
	else
		buffer_start = (int) vga_screen_front_buffer;

	/* Preprocessing inputs */
	if (steep > 0) {
		// Swap x_0 and y_0
		error = x_0;
		x_0 = y_0;
		y_0 = error;
		// Swap x_1 and y_1
		error = x_1;
		x_1 = y_1;
		y_1 = error;
	}
	if (x_0 > x_1) {
		// Swap x_0 and x_1
		error = x_0;
		x_0 = x_1;
		x_1 = error;
		// Swap y_0 and y_1
		error = y_0;
		y_0 = y_1;
		y_1 = error;
	}

	/* Setup local variables */
	deltax = x_1 - x_0;
	deltay = ABS(y_1 - y_0);
	error = -(deltax / 2);
	y = y_0;
	if (y_0 < y_1)
		ystep = 1;
	else
		ystep = -1;

	/* Draw a line - either go along the x-axis (steep = 0) or along the y-axis (steep = 1). The code is replicated to
	 * be fast on low optimization levels. */
	if (steep == 1)
	{
		for (x=x_0; x <= x_1; x++) {
			helper_plot_pixel(buffer_start, y, x, line_color);
			error = error + deltay;
			if (error > 0) {
				y = y + ystep;
				error = error - deltax;
			}
		}
	}
	else
	{
		for (x=x_0; x <= x_1; x++)
		{
			helper_plot_pixel(buffer_start, x, y, line_color);
			error = error + deltay;
			if (error > 0) {
				y = y + ystep;
				error = error - deltax;
			}
		}
	}
}

void VGA_text(int x, int y, char * text_ptr)
{
	int offset;

	/* assume that the text string fits on one line */
	offset = (y << 7) + x;
	while ( *(text_ptr) ) {
		*(character_buffer+ offset) = *(text_ptr);
		// write to the character buffer
		++text_ptr;
		++offset;
	}
}
/****************************************************************************************
* Set a single pixel on the VGA monitor
****************************************************************************************/
void VGA_pixel(int x, int y, short pixel_color){
	int offset;
	volatile short * pixel_buffer = (short *) 0x08000000;
	// VGA pixel buffer
	offset = (y << 9) + x;
	*(pixel_buffer + offset) = pixel_color;
}
/****************************************************************************************
* Clear screen - Set entire screen to black on the VGA monitor
****************************************************************************************/
void VGA_clear() {
	int x, y;
	for (x = 0; x < 160; x++) {
		for (y = 0; y < 240; y++) {
			VGA_pixel(x, y, 0);
		}
	}
}


