#include <alt_types.h>
#include <stdio.h>
#include "includes.h"
#include "math.h"
#include "altera_up_avalon_ps2.h"
#include "altera_up_ps2_keyboard.h"

/* Definition of Task Stacks */
#define   TASK_STACKSIZE       2048
OS_STK    task1_stk[TASK_STACKSIZE];
OS_STK    task2_stk[TASK_STACKSIZE];
OS_STK    paddle_stk[TASK_STACKSIZE];
OS_STK    ball_stk[TASK_STACKSIZE];
OS_STK    score_stk[TASK_STACKSIZE];

/* Screen size. */
#define RESOLUTION_X	320
#define RESOLUTION_Y	240
#define ABS(x)			(((x) > 0) ? (x) : -(x))

/* Definition of Task Priorities */
#define TASK1_PRIORITY      3	// GameState
#define TASK2_PRIORITY      2	// IO
#define BALL_PRIORITY       4
#define SCORE_PRIORITY      7
#define PADDLE_PRIORITY		5

/* Flag Events */
#define START_GAME   0x01
#define RUNNING_GAME 0x02
#define END_GAME     0x04
#define SPEED_GAME   0x08

// flag definitions
OS_FLAGS gameFlag;
OS_FLAG_GRP *GameStatus;

volatile int * KEY_ptr			= (int *) 0xFF200050;	// pushbutton KEY address used for testing
volatile int * SW_ptr			= (int *) 0xFF200040;	// switch slider address
volatile int * LEDR_ptr 		= (int *) 0xFF200000;	// LED RED address

// semaphore definitions
OS_EVENT *SemBallSpeed;

// mailbox definitions
OS_EVENT *Score1Mbox;
OS_EVENT *Score2Mbox;
OS_EVENT *GameSpeedMbox;

/* Global Variables */
int x1 = 10, x2 = 308;	// location of p1 and p2 on x-axis
int player1_y_0 = 110, player1_y_1 = 140;	// p1 paddle size
int player2_y_0 = 110, player2_y_1 = 140;	// p2 paddle size
int speed = 5;

/* Keyboard Definitions */
alt_u8 buf;
char ascii;
KB_CODE_TYPE *decode_mode;
char *inputStr;
alt_up_ps2_dev *keyboard;

/* Game state variables */
typedef enum { START, RUNNING, END } TimerStates;
static TimerStates CurTimerState = START;

/* Ball attributes */
struct {	// ball stuff
	int x, y; // positions
	struct {
		int x, y; // speed
	} speed;
	enum {	// direction
		LEFT_DOWN = 1,
		LEFT_UP,
		RIGHT_DOWN,
		RIGHT_UP,
		LEFT,
		RIGHT
	} direction;
} ball;

/* SD Card Definitions */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "Altera_UP_SD_Card_Avalon_Interface.h"

#define MAX_SUBDIRECTORIES 20

alt_up_sd_card_dev * sd_card;

/* Audio Definitions */
#include "system.h"
#include "altera_up_avalon_audio.h"
//#include "sys/alt_stdio.h"

/* globals AUDIO */
#define BUF_SIZE 50000			// about 10 seconds of buffer (@ 8K samples/sec)
#define BUF_THRESHOLD 96		// 75% of 128 word buffer
#define sample_size 8000
alt_up_audio_dev *audio_dev;
unsigned int l_buf[BUF_SIZE];
unsigned int r_buf[BUF_SIZE];

//track file names
char chop[32] = "BEEP1.WAV";

/* globals AUDIO */
#define BUF_SIZE 50000			// about 10 seconds of buffer (@ 8K samples/sec)
#define BUF_THRESHOLD 96		// 75% of 128 word buffer
alt_up_audio_dev *audio_dev;

void start_screen(void)
{
	VGA_text(15, 5, "                ");
	VGA_text(15, 6, "                ");
	VGA_text(310, 3, "                   ");
	VGA_text(310, 4, "                   ");
	VGA_text(25, 30, "                          ");
	VGA_clear();
	draw_line(x1, player1_y_0, x1, player1_y_1, 0,0);
	draw_line(x2, player2_y_0, x2, player2_y_1, 0,0);
	VGA_text(32, 15, "PONG THE GAME");
	VGA_text(27, 29, "PRESS KEY 3 TO START GAME");
}


/* STATES THE GAME IS IN */
void GameStateTask(void* pdata)
{
	clear_screen(0);
	INT8U err;

	while (1)
	{
		if (CurTimerState == START) {
			gameFlag = OSFlagAccept(GameStatus, START_GAME, OS_FLAG_WAIT_SET_ALL, &err);
			start_screen();
			switch(err) {
			case OS_NO_ERR:
				if(*(KEY_ptr) & 0x08) {
					CurTimerState = RUNNING;
					VGA_text(25, 29, "                                     ");
					VGA_text(25, 15, "                                     ");
					//printf("running state");
				}
				break;
			case OS_FLAG_ERR_NOT_RDY:
				CurTimerState = START;
				start_screen();
				break;
			default:
				printf("ERROR IN START STATE\n");
			}
		}

		if(CurTimerState == END) {
			gameFlag = OSFlagAccept(GameStatus, END_GAME, OS_FLAG_WAIT_SET_ALL, &err);
			switch(err) {
			case OS_NO_ERR:
				if(decode_scancode(keyboard, &decode_mode, &buf, &ascii)==0){
					if(ascii == 'R'){
						CurTimerState = START;
					}
				}
				break;
			case OS_FLAG_ERR_NOT_RDY:
				CurTimerState = END;OSTimeDlyHMSM(0, 0, 0, 125);
				break;
			default:
				printf("ERROR IN END STATE\n");
			}
		}
		OSTimeDlyHMSM(0, 0, 0, 125);
	}
}


/* Handles switches for speed of the ball */
void IOTask(void* pdata)
{
	INT8U err;

	init_audio();
	init_sd_card();

	find_files ("."); //shows files but kinda weird

	readWavFile(&(chop));

	while(1) {
		if (*(SW_ptr) & 0x08) {	// switch 4 to start the game
			gameFlag = OSFlagPost(GameStatus, START_GAME, OS_FLAG_SET, &err);
			*(LEDR_ptr) |= 0x08;	// turn on LEDR0
		} else {
			gameFlag = OSFlagPost(GameStatus, START_GAME, OS_FLAG_CLR, &err);
			*(LEDR_ptr) &= ~(0x08);	// turn off LEDR0
		}

/*		if(*(SW_ptr) & 0x01) {	// SW0 easy
			speed = 3;
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_SET, &err);
			*(LEDR_ptr) |= 0x01;	// turn on LEDR0
		} else {
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_CLR, &err);
			*(LEDR_ptr) &= ~(0x01);	// turn off LEDR0
		}*/
/*		if(*(SW_ptr) & 0x02) {	// SW1 medium
			speed = 5;
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_SET, &err);
			*(LEDR_ptr) |= 0x02;	// LEDR1
		} else {
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_CLR, &err);
			*(LEDR_ptr) &= ~(0x02);		// off LEDR1
		}*/
/*		if(*(SW_ptr) & 0x04) {	// SW1 hard
			speed = 7;
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_SET, &err);
			*(LEDR_ptr) |= 0x04;	// LEDR1

		} else {
			gameFlag = OSFlagPost(GameStatus, SPEED_GAME, OS_FLAG_CLR, &err);
			*(LEDR_ptr) &= ~(0x04);		// off LEDR1
		}*/

		OSMboxPost(GameSpeedMbox, (void *) &speed);
		OSTimeDlyHMSM(0, 0, 0, 125);
	}
}


/* Handles the paddle movement */
void PaddleTask(void* pdata)
{
	clear_screen(0);

	while(1) {
		if(CurTimerState == RUNNING) {
			// Wait for redraw cycle
			//wait_for_vsync();
			// Erase old line
			draw_line(x1, player1_y_0, x1, player1_y_1, 0,0);
			draw_line(x2, player2_y_0, x2, player2_y_1, 0,0);

			// flag to start the game
			if(decode_scancode(keyboard, &decode_mode, &buf, &ascii)==0){
				if (decode_mode == KB_ASCII_MAKE_CODE) {
					if(ascii == 'W'){
						if (player1_y_0 > 0)	// has not reached the top border
						{
							player1_y_0 -= (ball.speed.y * 2);
							player1_y_1 -= (ball.speed.y * 2);
							ascii = 0;
							alt_up_ps2_clear_fifo(keyboard);
						}
					} else if(ascii == 'S'){
						if (player1_y_1 < RESOLUTION_Y)	// has not reached the bottom border
						{
							player1_y_0 += (ball.speed.y * 2);
							player1_y_1 += (ball.speed.y * 2);
							ascii = 0;
							alt_up_ps2_clear_fifo(keyboard);
						}
					} else if(ascii == 'I'){
						if (player2_y_0 > 0)	// has not reached the top border
						{
							player2_y_0 -= (ball.speed.y * 2);
							player2_y_1 -= (ball.speed.y * 2);
							ascii = 0;
							//alt_up_ps2_clear_fifo(keyboard);
						}
					} else if(ascii == 'K'){
						if (player2_y_1 < RESOLUTION_Y)	// has not reached the bottom border
						{
							player2_y_0 += (ball.speed.y * 2);
							player2_y_1 += (ball.speed.y * 2);
							ascii = 0;
							//alt_up_ps2_clear_fifo(keyboard);
						}
					} else{
						//do nothing
					}
				}
			}

			if (*(KEY_ptr) & 0x02) {
				if (player2_y_0 > 0)	// has not reached the top border
					{
						player2_y_0 -= (ball.speed.y * 2);
						player2_y_1 -= (ball.speed.y * 2);
					}
			} else if (*(KEY_ptr) & 0x04) {
				if (player2_y_1 < RESOLUTION_Y)	// has not reached the bottom border
					{
						player2_y_0 += (ball.speed.y * 2);
						player2_y_1 += (ball.speed.y * 2);
					}
			}

			// Draw new line
			draw_line(x1, player1_y_0, x1, player1_y_1, 0xffff,0);
			draw_line(x2, player2_y_0, x2, player2_y_1, 0xffff,0);
			OSTimeDlyHMSM(0, 0, 0, 125);
		}
	}
}

/* COLLISION WITH BORDERS */
void collide_wall(void)
{
	if(ball.direction == RIGHT_UP) {
		ball.direction = RIGHT_DOWN;
	} else if(ball.direction == RIGHT_DOWN) {
		ball.direction = RIGHT_UP;
	} else if(ball.direction == LEFT_DOWN) {
		ball.direction = LEFT_UP;
	} else if(ball.direction == LEFT_UP) {
		ball.direction = LEFT_DOWN;
	}
}

/* COLLISION WITH PADDLES */
void collide_paddle(void) {
	if(ball.direction == RIGHT_UP) {
		ball.direction = LEFT_UP;
	} else if(ball.direction == RIGHT_DOWN) {
		ball.direction = LEFT_DOWN;
	} else if(ball.direction == LEFT_DOWN) {
		ball.direction = RIGHT_DOWN;
	} else if(ball.direction == LEFT_UP) {
		ball.direction = RIGHT_UP;
	} else if(ball.direction == LEFT) {
		ball.direction = RIGHT;
	} else if (ball.direction == RIGHT) {
		ball.direction = LEFT;
	}
}

/* BALL MOVEMENT */
void ball_movement(void) {
	// ball movement
	// possibly add more angles
	if(ball.direction == LEFT_DOWN) {
		ball.x -= ball.speed.x;
		ball.y += ball.speed.y;
	} else if(ball.direction == LEFT_UP) {
		ball.x -= ball.speed.x;
		ball.y -= ball.speed.y;
	} else if(ball.direction == RIGHT_UP) {
		ball.x += ball.speed.x;
		ball.y -= ball.speed.y;
	} else if(ball.direction == RIGHT_DOWN) {
		ball.x += ball.speed.x;
		ball.y += ball.speed.y;
	} else if (ball.direction == LEFT) {
		ball.x -= ball.speed.x;
	} else if (ball.direction == RIGHT) {
		ball.x += ball.speed.x;
	}
}

/* Handles the ball */
void BallTask(void* pdata)
{
	//INT8U err;
	//int *speed;
	//speed = OSMboxAccept(GameSpeedMbox);
	ball.x = RESOLUTION_X/2;
	ball.y = RESOLUTION_Y/2;
	ball.speed.x = speed;
	ball.speed.y = speed;
	ball.x += ball.speed.x;
	ball.y += ball.speed.y;
	ball.direction = RIGHT_DOWN;
	int r = 1;
	int score1 = 0, score2 = 0;
	for(;;) {
		if(CurTimerState == RUNNING) {
			// Wait for redraw cycle
			//wait_for_vsync();
			// erase the ball
			circleBres(ball.x, ball.y, r, 0);
			// ball collision with the paddles and the top and bottom borders (FUNCTIONAL ATM!!!!)
			if(((ball.x+r-5 <= x1) && (ball.y >= player1_y_0) && (ball.y <= player1_y_1)) ) {
				playBuffer();
				collide_paddle();
			}
			if((ball.x+r+4 >= x2) && (ball.y >= player2_y_0) && (ball.y <= player2_y_1)) {
				playBuffer();

				collide_paddle();
			}

			// top and bottom borders
			if(ball.y + r <= 0 || ball.y + r >= RESOLUTION_Y) {
				playBuffer();
				collide_wall();
			}

			// resets the ball to the middle
			// score for p2
			if(ball.x >= RESOLUTION_X) {
				score2++;
				ball.x = RESOLUTION_X/2;
				ball.y = RESOLUTION_Y/2;
			}
			// score for p1
			if(ball.x <= 0) {
				score1++;
				ball.x = RESOLUTION_X/2;
				ball.y = RESOLUTION_Y/2;
			}

			ball_movement();

			OSMboxPost(Score1Mbox, (void *) &score1);
			OSMboxPost(Score2Mbox, (void *) &score2);
			circleBres(ball.x, ball.y, r, 0xffff);
			OSTimeDlyHMSM(0, 0, 0, 125);
		}
	}
}

/* Handle the scores for player 1 and player 2 */
void ScoreTask(void* pdata)
{
	INT8U err;
	int *score1, *score2;
	char texts[30];
	char text2[30];
	clear_screen(0);
	// use a mailbox to determine the score
	for(;;) {
		if (CurTimerState == RUNNING) {
			// mailbox the scores of player 1 and 2
			score1 = OSMboxAccept(Score1Mbox);
			score2 = OSMboxAccept(Score2Mbox);
			snprintf(texts, sizeof(texts), " %d", *score1);
			snprintf(text2, sizeof(text2), " %d", *score2);
			// display the score of player 1 and 2
			VGA_text(15, 5, "PLAYER 1");
			VGA_text(15, 6, "SCORE: ");
			VGA_text(21, 6, text2);
			VGA_text(310, 3, "PLAYER 2");
			VGA_text(310, 4, "SCORE: ");
			VGA_text(316, 4, texts);

			if (*score1 == 5) {		// score of player 1 set it to END_GAME
				gameFlag = OSFlagPost(GameStatus, END_GAME, OS_FLAG_SET, &err);
				CurTimerState = END;
				VGA_text(32, 29, "PLAYER 2 WINS");
				VGA_text(29, 30, "PRESS 'R' TO RESET");
				*score1 = 0;
				*score2 = 0;
			} else if (*score2 == 5) {	// score of p2 set it to END_GAME
				gameFlag = OSFlagPost(GameStatus, END_GAME, OS_FLAG_SET, &err);
				CurTimerState = END;
				VGA_text(32, 29, "PLAYER 1 WINS");
				VGA_text(29, 30, "PRESS 'R' TO RESET");
				*score1 = 0;
				*score2 = 0;
			}
		}
		OSTimeDlyHMSM(0, 0, 0, 125);
	}
}

// find_files will print out the list of files in the current path,
// then recursively call itself on any subdirectories it finds.
// It is limited to directories containing MAX_SUBDIRECTORIES or fewer
// subdirectories and a maximium path length of 104 characters (including /'s)
void find_files (char* path){
	char filepath [90];
	char filename [15];
	char fullpath [104];
	char* folders [MAX_SUBDIRECTORIES];
	int num_dirs = 0;
	short int file;
	short int attributes;
	bool foundAll;

	//copy the path name to local memory
	strcpy (filepath, path);

	foundAll = (alt_up_sd_card_find_first(filepath,filename) == 0 ? false : true);

	//output the current directory
	printf("/%s\n",filepath);

	//loop through the directory tree
	while (!foundAll){
		strcpy (fullpath,filepath);
		//remove the '.' character from the filepath (foo/bar/. -> foo/bar/)
		fullpath [strlen(filepath)-1] = '\0';
		strcat (fullpath,filename);

		file = alt_up_sd_card_fopen (fullpath, false);
		attributes = alt_up_sd_card_get_attributes (file);
		if (file != -1)
			alt_up_sd_card_fclose(file);

		//print the file name, unless it's a directory or mount point
		if ( (attributes != -1) && !(attributes & 0x0018)){
			printf("/%s\n",fullpath);
		}

		//if a directory is found, allocate space and save its name for later
		if ((attributes != -1) && (attributes & 0x0010)){
			folders [num_dirs] = malloc (15*sizeof(char));
			strcpy(folders[num_dirs],filename);
			num_dirs++;
		}

		foundAll = (alt_up_sd_card_find_next(filename) == 0 ? false : true);
	}

	//second loop to open any directories found and call find_files() on them
	int i;
	for (i=0; i<num_dirs; i++){

		strcpy (fullpath,filepath);
		fullpath [strlen(filepath)-1] = '\0';
		strcat (fullpath,folders[i]);
		strcat (fullpath, "/.");
		find_files (fullpath);
		free(folders[i]);
	}

	return;
}

void init_audio(void)
{
	audio_dev = alt_up_audio_open_dev ("/dev/Audio_Subsystem_Audio");

	// audio part
	if (audio_dev == NULL)	{
		printf("Error: could not open audio device\n");
	} else
		printf("Opened audio device\n");
}

void init_sd_card(void)
{
	alt_up_sd_card_dev * sd_card;
	sd_card = alt_up_sd_card_open_dev("/dev/SD_Card");

	if (sd_card!=NULL){
		if (alt_up_sd_card_is_Present()){
			printf("An SD Card was found!\n");
		}
		else {
			printf("No SD Card Found. \n Exiting the program.");
		}

		if (alt_up_sd_card_is_FAT16()){
			printf("FAT-16 partition found!\n");
		}
		else{
			printf("No FAT-16 partition found - Exiting!\n");
		}
	}
}

void readWavFile(char * wavFile){
	// opening up the .wav file
	int file_index_test2 = alt_up_sd_card_fopen(wavFile,false);
	if (file_index_test2 <0){
		printf("File Error: File not found");
	}
	else{
		char read_data,read_data2;
		for (int i = 0; i < 11; i++){ //skill over header
			// read returns a byte of data from .wav file
			alt_up_sd_card_read(file_index_test2);
			alt_up_sd_card_read(file_index_test2);
			alt_up_sd_card_read(file_index_test2);
			alt_up_sd_card_read(file_index_test2);
		}
		int i=0;
		unsigned int fileData;
		while (i < BUF_SIZE){	// BUF_SIZE depends on files
			read_data =  alt_up_sd_card_read(file_index_test2) ;
			read_data2 = alt_up_sd_card_read(file_index_test2) ;

			// has to be stored as int since fifo_space takes in an int type
			fileData = ((read_data2 << 8) + read_data)<<16 ;

			// store into left and right buffers
			l_buf[i]= fileData;
			r_buf[i] = fileData;
			i++;
			if (i%20000 == 0){ //print progress
				printf("samples from file: %i of %i\n",i,BUF_SIZE);
			}
		}
		alt_up_sd_card_fclose(file_index_test2);
	}
}

void playBuffer(void){
	int buffer_index = 0;
	int num_out = 0;
	while (buffer_index <BUF_SIZE){
		// play has write to fifo in it so no need to use fifo_write
		num_out = alt_up_audio_play_r (audio_dev, &(r_buf[buffer_index]), BUF_SIZE-buffer_index);
		alt_up_audio_play_l (audio_dev, &(l_buf[buffer_index]), BUF_SIZE-buffer_index);
		//printf("wrote %i words to fifo",num_out);
		buffer_index += num_out;
		break;
	}
}




/* The main function creates two task and starts multi-tasking */
int main(void)
{
	keyboard = alt_up_ps2_open_dev("/dev/PS2_Port");
	INT8U err;
	//SemBallSpeed = OSSemCreate(1);
	GameSpeedMbox = OSMboxCreate((void *)0);
	Score1Mbox = OSMboxCreate((void *)0);
	Score2Mbox = OSMboxCreate((void *)0);
	GameStatus = OSFlagCreate(0x00, &err);

	OSTaskCreateExt(GameStateTask,
                  NULL,
                  (void *)&task1_stk[TASK_STACKSIZE-1],
                  TASK1_PRIORITY,
                  TASK1_PRIORITY,
                  task1_stk,
                  TASK_STACKSIZE,
                  NULL,
                  0);


  OSTaskCreateExt(IOTask,
                  NULL,
                  (void *)&task2_stk[TASK_STACKSIZE-1],
                  TASK2_PRIORITY,
                  TASK2_PRIORITY,
                  task2_stk,
                  TASK_STACKSIZE,
                  NULL,
                  0);

  OSTaskCreateExt(PaddleTask,
                  NULL,
                  (void *)&paddle_stk[TASK_STACKSIZE-1],
                  PADDLE_PRIORITY,
                  PADDLE_PRIORITY,
                  paddle_stk,
                  TASK_STACKSIZE,
                  NULL,
                  0);

  OSTaskCreateExt(BallTask,
                  NULL,
                  (void *)&ball_stk[TASK_STACKSIZE-1],
                  BALL_PRIORITY,
                  BALL_PRIORITY,
                  ball_stk,
                  TASK_STACKSIZE,
                  NULL,
                  0);

  OSTaskCreateExt(ScoreTask,
				  NULL,
				  (void *)&score_stk[TASK_STACKSIZE-1],
				  SCORE_PRIORITY,
				  SCORE_PRIORITY,
				  score_stk,
				  TASK_STACKSIZE,
				  NULL,
				  0);


  OSStart();
  return 0;
}
