#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>

#include "vga_ioctl.h"
#include "timer_ioctl.h"

#define X_MAX 639
#define Y_MAX 479
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT * 4)
#define IMAGE 0
#define RECT 1
#define READ_FREQ 100000000
#define MEGABYTE (1024*1024)
#define MINUTE 60
#define HOUR (60*MINUTE)
#define DAY (24*HOUR)
#define BACKSPACE_CHAR 127
#define MICROSECONDS_PER_SECOND 1000000
#define SCREEN_PARTITION 394
#define CHAR_WIDTH 10
#define CHAR_HEIGHT 21
#define ASCII_SHEET_WIDTH 192
#define ASCII_SHEET_HEIGHT 368
#define ASCII_SHEET_CHAR_WIDTH 12
#define ASCII_SHEET_CHAR_HEIGHT 23
#define CHARS_PER_LINE (SCREEN_WIDTH / CHAR_WIDTH)
#define MAX_FILENAME_LENGTH 20
#define MONITOR_BACKGROUND_COLOUR 0xFF9C3C13

struct Params {
	int imageHeight;
	int imageWidth;
	int imageYStart;
	int imageXStart; 
	int imageYEnd;
	int imageXEnd;
	int xStart;
	int yStart;
	int x;
	int y;
};

struct Params setParams(struct Params params);
void printUserImage(struct Params params);
void loadImage(struct Params params, char filename[MAX_FILENAME_LENGTH], int pixelSize);
void printImage(struct Params params, int pixelSize, int colour, bool blend);
void writeToScreen(char alpha, char red, char green, char blue, int *pixelNum, bool blend);
void printUserRectangle(struct Params params);
void printConsole();
void printChar(struct Params params, char c, bool blend);
void clearScreen(int endLine);
void startTimer(int fd);
double readTimer(int fd);
void *performanceMonitor(void *arg);
void printString(char str[CHARS_PER_LINE * 4 + 1]);
// global variables for the pointers to the image data and vga buffer
int *buffer;
char *imagep;
// global variables for performance monitor, need access between threads
// these should be protected by a mutex
int readCounter = 0;
int writeCounter = 0;
double writeTime = 0;
double readTime = 0;
double appStartTime = 0;

pthread_mutex_t lock;

int main(int argc, char *argv[])
{
	char prompt;
	int fd, mode;
	struct Params params;
	time_t appStart;
	appStartTime = time(&appStart);

	// initialize vga driver and screen buffer
	fd = open("/dev/vga_driver",O_RDWR);

	if (fd == -1) {
		perror("failed to open vga driver");
		return 1;
	}
	buffer = (int*)mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	// prompt for initial parameters
	while(1){
		printf("\nClear the screen? (y/n) ");
		scanf(" %c", &prompt);
		if(prompt == 'y'){
			clearScreen(Y_MAX);
		}
		printf("\nPrint a string? (y/n) ");
		scanf(" %c", &prompt);
		if(prompt == 'y'){
			printConsole();
			free(imagep);
			continue;
		}
		printf("\nPrint an image? (y/n) ");
		scanf(" %c", &prompt);
		if(prompt == 'y'){
			mode = IMAGE;
		}
		else {
			printf("\nPrint a rectangle? (y/n) ");
			scanf(" %c", &prompt);
			if(prompt == 'y'){
				mode = RECT;
			}
			else{
				continue;
			}
		}
		printf("\nEnter the width:");
		scanf("%d", &params.imageWidth);
		printf("\nEnter the height:");
		scanf("%d", &params.imageHeight);
		printf("\nEnter the x,y location:");
		scanf("%d %d", &params.x, &params.y);
		// set default parameter values (no sub image)
		params.imageXStart = 0;
		params.imageYStart = 0;
		params.imageXEnd = params.imageWidth;
		params.imageYEnd = params.imageHeight;
		// print the rectangle/image
		if(mode == IMAGE){
			printUserImage(params);
			free(imagep);
		}
		if(mode == RECT){
			printUserRectangle(params);
		}
	}
	return 0;
}
struct Params setParams(struct Params params){
	// image is not on the screen, we have an error
	if(params.y > Y_MAX || params.x > X_MAX || params.y + params.imageHeight < 0 || 
		params.x + params.imageWidth < 0){
		// set y start and end to the same value so that printImage()
		// will not try to print the image off the screen
		params.imageYStart = 0;
		params.imageYEnd = 0;
		printf("Image not on screen.");
	}
	else{
		// base case where the whole image appears on the screen
		params.yStart = params.y;
		params.xStart = params.x;
		// cases when the image is partly on the screen
		if(params.y + params.imageYEnd - params.imageYStart > Y_MAX){
			params.imageYEnd = Y_MAX - params.y;
			printf("Image partially on screen.");
		}
		if(params.x + params.imageXEnd - params.imageXStart > X_MAX){
			params.imageXEnd = X_MAX - params.x;
			printf("Image partially on screen.");
		}
		if(params.y < 0){
			if(params.imageYStart < -params.yStart){
				params.imageYStart = -params.yStart;
			}
			params.yStart = 0;
			printf("Image partially on screen.");
		}
		if(params.x < 0){
			if(params.imageXStart < -params.xStart){
				params.imageXStart = -params.xStart;
			}
			params.xStart = 0;
			printf("Image partially on screen.");
		}
	}
	return params;	
}
void printUserImage(struct Params params){
	char prompt;
	char filename[MAX_FILENAME_LENGTH];

	printf("\nEnter the name of the file: ");
	scanf(" %s", &filename);
	printf("\nDo you want to print a sub image (y/n)? ");
	scanf(" %c", &prompt);
	if(prompt == 'y'){
		printf("\nEnter the x and y start and end points (xStart xEnd yStart yEnd). ");
		scanf("%d %d %d %d", &params.imageXStart, &params.imageXEnd, 
			&params.imageYStart, &params.imageYEnd);
	}
	params = setParams(params);
	printf("\nDoes the image have an alpha layer? (y/n)? ");
	scanf(" %c", &prompt);
	if(prompt == 'y'){
		loadImage(params, filename, 4);
		// set colour to 0 so that the function knows we want an image
		printImage(params, 4, 0, true);
	}
	else{
		loadImage(params, filename, 3);
		printImage(params, 3, 0, true);
	}
}
void loadImage(struct Params params, char filename[MAX_FILENAME_LENGTH], int pixelSize){
	FILE *fileptr;
	long filelen;
	// calculate the file length using the given parameters
	filelen = params.imageHeight*params.imageWidth*pixelSize;
	fileptr = fopen(filename, "rb");
	// update the image pointer
	imagep = (char *)malloc((filelen+1)*sizeof(char));
	fread(imagep, filelen, 1, fileptr);
	fclose(fileptr);
}
// for printing images colour should be 0
void printImage(struct Params params, int pixelSize, int colour, bool blend){
	int xCurr = params.xStart;
	int yCurr = params.yStart;
	int i, j;
	char pixelAlpha, pixelRed, pixelGreen, pixelBlue;
	// do we need this??
	// params.imageHeight--;
	// params.imageWidth--;
	// loop through only pixels that will appear on the screen
	for(j = params.imageYStart; j <= params.imageYEnd; j++){
		for(i = params.imageXStart; i <= params.imageXEnd; i++){
			// if the colour is 0 that means we have an image
			// in which case we read the image for its particular format (alpha/no alpha)
			if(colour == 0){
				if(pixelSize == 3){
					pixelAlpha = 0xFF;
				}
				else{
					pixelAlpha = *(imagep + i * pixelSize + j * params.imageWidth * pixelSize + 3);
				}
				pixelRed = *(imagep + i * pixelSize + j * params.imageWidth * pixelSize);
				pixelGreen = *(imagep + i * pixelSize + j * params.imageWidth * pixelSize + 1);
				pixelBlue = *(imagep + i * pixelSize + j * params.imageWidth * pixelSize + 2);
			}
			else{
				pixelAlpha = colour >> 24;
				pixelRed = colour >> 16;
				pixelGreen = colour >> 8;
				pixelBlue = colour;
			}
		
			writeToScreen(pixelAlpha, pixelRed, pixelGreen, pixelBlue, 
				(buffer + xCurr + yCurr*SCREEN_WIDTH), blend);
		
			// advance the pointers to the current pixel
			if(i == params.imageXEnd){
				xCurr = params.xStart;
				yCurr++;
			}
			else {
				xCurr++;
			}
		}
	}
}
void writeToScreen(char alpha, char red, char green, char blue, int *pixelNum, bool blend){
	char vgaAplha, vgaRed, vgaGreen, vgaBlue;
	int pixel;
	float alpha_f;
	vgaAplha = 0xFF;
	if(blend){
		// read pixel from screen
		pixel = *pixelNum;
	}
	else{
		// no alpha blending, assume pixel is blue
		pixel = MONITOR_BACKGROUND_COLOUR;
	}
	vgaRed = pixel;
	vgaGreen = pixel >> 8;
	vgaBlue = pixel >> 16;
	// alpha blending
	alpha_f = (float)alpha/255;
	vgaRed = red*alpha_f + (1-alpha_f)*vgaRed;
	vgaGreen = green*alpha_f + (1-alpha_f)*vgaGreen;
	vgaBlue = blue*alpha_f + (1-alpha_f)*vgaBlue;
	// format the new pixel to write to the screen 
	// screen is in ABGR format!!
	pixel = ((int)vgaAplha) << 24 | ((int)vgaBlue) << 16 | 
			((int)vgaGreen) << 8 | (int)vgaRed;

	*pixelNum = pixel;
}
void printUserRectangle(struct Params params){
	int colour = 0;

	printf("\nEnter the rectangle colour in hex format (0xaaRRGGBB):");
	scanf("%x", &colour);
	while(colour == 0){
		printf("\nColour must be a non zero number!");
		printf("\nEnter the rectangle colour in hex format (0xaaRRGGBB):");
		scanf("%x", &colour);
	}
	params = setParams(params);
	// we don't care about the value for pixelSize since this is a rectangle
	printImage(params, 0, colour, true);
}
void printConsole(){
	int readFlag, serial_fd, timer_fd, flags;
	char c;
	struct termios tio;
	struct Params params, rect;
	double currRead;
	tcflag_t restore;
	pthread_t monitorThread;

	// set flag to true initially
	// create a separate thread for the performance monitor
	if(pthread_create(&monitorThread, NULL, performanceMonitor, NULL)){
		printf("Error creating thread\n");
		return;
	}
	
	// setup the UART to read chars
	memset(&tio, 0, sizeof(tio));
	serial_fd = open("/dev/ttyPS0",O_RDWR);
	if(serial_fd == -1)printf("Failed to open serial port... :( \n");
	// get terminal attributes and store into tio	
	tcgetattr(serial_fd, &tio);
	// set the i/o baud rate
	cfsetospeed(&tio, B115200);
	cfsetispeed(&tio, B115200);
	// save the original state of the flag
	restore = tio.c_lflag;
	// disable canonical mode
	tio.c_lflag = tio.c_lflag & ~(ICANON);
	// set the terminal attributes NOW (TCSANOW)
	tcsetattr(serial_fd, TCSANOW, &tio);
	// set serial port to non blocking
	if (-1 == (flags = fcntl(serial_fd, F_GETFL, 0)))
        	flags = 0;
	fcntl(serial_fd, F_SETFL, flags | O_NONBLOCK);

	// open the timer driver
	if (!(timer_fd = open("/dev/timer_driver", O_RDWR))) {
		perror("Failed to initiallize timer driver.");
		exit(EXIT_FAILURE);
	}
	// set parameters for the ASCII character sheet
	params.x = 0;
	params.y = 0;
	params.imageWidth = ASCII_SHEET_WIDTH;
	params.imageHeight = ASCII_SHEET_HEIGHT;
	// load the image, it has an alpha layer so pixels are 4 bytes each
	loadImage(params, "example2.raw", 4);
	// set read flag to 2 initially so that the timer starts
	readFlag = 2;
	currRead = 0;
	while(1){
		// start the timer when the system has read a character
		if(readFlag != 0 && readFlag != -1){
			startTimer(timer_fd);
		}
		readFlag = read(serial_fd, &c, 1);
		// if the timer reaches the maximum value (~45s) add the time taken to the total
		// and restart the timer
		if(readTimer(timer_fd) == 0){
			pthread_mutex_lock(&lock);
			readTime += currRead;
			pthread_mutex_unlock(&lock);
			startTimer(timer_fd);
		}
		else{
			currRead = readTimer(timer_fd);
		}
		// if there is no input the flag will be 0, -1 if there is an error
		// in either case we don't try to print the character
		if(readFlag != 0 && readFlag != -1){
			// update values for performance monitor
			pthread_mutex_lock(&lock);
			readCounter++;
			readTime += currRead;
			pthread_mutex_unlock(&lock);
			// if the user enters a ! then the system stops
			if(c == '!'){
				break;
			}
			// if we've received a new line character or reached the end of the screen
			// start a new line
			if(c == '\n' || params.x >= SCREEN_WIDTH){
				// increment y 1 extra pixel for the first 17 lines
				// so that the lines look more evenly aligned
				if(params.y >= (CHAR_HEIGHT + 1) * 17){
					params.y += CHAR_HEIGHT;
				}
				else{
					params.y += CHAR_HEIGHT + 1;
				}
				params.x = 0;
				continue;
			}
			// 
			if(c == BACKSPACE_CHAR){
				// as long as we have a character on the screen we need to backspace
				if(!(params.x == 0 && params.y == 0)){
					// if x > 0 we can move back a character on the same line
					// else we have to move to the previous line
					if(params.x > 0){
						params.x -= CHAR_WIDTH;
					}
					else{
						if(params.y >= (CHAR_HEIGHT + 1) * 17){
							params.y -= CHAR_HEIGHT;
						}
						else{
							params.y -= CHAR_HEIGHT + 1;
						}
						params.x = SCREEN_WIDTH - CHAR_WIDTH;
					}
					rect.x = params.x;
					rect.y = params.y;
					rect.imageXStart = 0;
					rect.imageYStart = 0;
					rect.imageXEnd = CHAR_WIDTH;
					rect.imageWidth = CHAR_WIDTH;
					rect.imageYEnd = CHAR_HEIGHT + 1;
					rect.imageHeight = CHAR_HEIGHT + 1;
					rect = setParams(rect);
					// we don't care about the value for pixelSize since this is a rectangle
					printImage(rect, 0, 0xFF000000, true);
					continue;
				}
			}
			// if we've reached the end of the screen, clear it and start again
			if(params.y >= SCREEN_PARTITION){
				clearScreen(SCREEN_PARTITION);
				params.y = 0;
			}
			// start the timer to get the write time
			startTimer(timer_fd);
			printChar(params, c, true);
			// after writing to the screen update the write timer and counter
			pthread_mutex_lock(&lock);
			writeTime += MICROSECONDS_PER_SECOND * readTimer(timer_fd);
			writeCounter++;
			pthread_mutex_unlock(&lock);
			// after each character is printed increment the x location
			params.x += CHAR_WIDTH;
		}
	}
	// re-enable canonical mode
	tio.c_lflag = restore;
	// set the terminal attributes NOW (TCSANOW)
	tcsetattr(serial_fd, TCSANOW, &tio);
}
void printChar(struct Params params, char c, bool blend){
	int row, col;
	// character sheet is 16x16
	// grab the location of each character on the sheet
	col = c % 16;
	row = c / 16;
	// set the default values for the printing parameters
	params.xStart = params.x;
	params.yStart = params.y;
	// print a sub images for the character we want
	params.imageXStart = col * ASCII_SHEET_CHAR_WIDTH + 1;
	params.imageXEnd = params.imageXStart + CHAR_WIDTH;
	params.imageYStart = row * ASCII_SHEET_CHAR_HEIGHT + 1;
	params.imageYEnd = params.imageYStart + CHAR_HEIGHT;
	
	printImage(params, 4, 0, blend);
}
void clearScreen(int endLine){
	int i, j;
	for (i = 0; i <= endLine; i++){
		for (j = 0; j < SCREEN_WIDTH; j++){
			*(buffer + j + i * SCREEN_WIDTH) = 0x0;
		}
	}
}
void startTimer(int fd){
	struct timer_ioctl_data data;

	// Rest the counter
	data.offset = LOAD_REG;
	data.data = 0x0;
	if (ioctl(fd, TIMER_WRITE_REG, &data) == -1)
		printf("Ioctl failed - Failed to rest the counter \n");					

	// Set control bits to load value in load register into counter
	data.offset = CONTROL_REG;
	data.data = LOAD0;
	if(ioctl(fd, TIMER_WRITE_REG, &data) == -1)
		printf("Ioctl failed - Failed to set control bits \n");					

	// Set control bits to enable timer, count up
	data.offset = CONTROL_REG;
	data.data = ENT0;
	if(ioctl(fd, TIMER_WRITE_REG, &data) == -1)
		printf("Ioctl failed - Failed to start counting \n");
}
double readTimer(int fd){
	struct timer_ioctl_data data;

	data.offset = TIMER_REG;
	ioctl(fd, TIMER_READ_REG, &data);
	return ((double)data.data) / READ_FREQ;
}
void *performanceMonitor(void *arg){
	time_t appCurrent;
	struct sysinfo si;
	struct Params params;
	double appTime, appCurrentTime, avgRead, avgWrite, totalRAM, freeRAM, avgReadDec, avgWriteDec;
	int uptimeDay, uptimeHour, uptimeMinute, uptimeSecond, appTimeHour, appTimeMinute, appTimeSecond, 
		avgReadInt, avgWriteInt, i, processCount;
	char output[CHARS_PER_LINE * 4 + 1];
	char temp[CHARS_PER_LINE + 1];
	// need to load the image again for the second thread
	// set parameters for the ASCII character sheet
	params.x = 0;
	params.y = 0;
	params.imageWidth = ASCII_SHEET_WIDTH;
	params.imageHeight = ASCII_SHEET_HEIGHT;
	// load the image, it has an alpha layer so pixels are 4 bytes each
	loadImage(params, "example2.raw", 4);

	while(1){
		
		appCurrentTime = time(&appCurrent);
		appTime = appCurrentTime - appStartTime;
		pthread_mutex_lock(&lock);
		// average read and write time without dividing by zero
		if(readCounter == 0){
			avgRead = 0;
		}
		else{
			avgRead = readTime / readCounter;
		}
		if(writeCounter == 0){
			avgWrite = 0;
		}
		else{
			avgWrite = writeTime / writeCounter;
		}
		pthread_mutex_unlock(&lock);

		appTimeHour = ((int)appTime % DAY) / HOUR;
		appTimeMinute = ((int)appTime % HOUR) / MINUTE;
		appTimeSecond = (int)appTime % MINUTE;

		sysinfo(&si);
		uptimeDay = si.uptime / DAY;
		uptimeHour = (si.uptime % DAY) / HOUR;
		uptimeMinute = (si.uptime % HOUR) / MINUTE;
		uptimeSecond = si.uptime % MINUTE;		

		// ram totals in megabytes
		totalRAM = si.totalram / MEGABYTE;
		freeRAM = si.freeram / MEGABYTE;

		processCount = si.procs;
		
		// split the averages into integer and decimal parts
		// in order to easily display fixed width numbers
		avgReadInt = (int)avgRead;
		avgReadDec = avgRead - avgReadInt;
		avgWriteInt = (int)avgWrite;
		avgWriteDec = avgWrite - avgWriteInt;
		// prevent numbers from overflowing the display
		if(avgWriteInt > 9999){
			avgWriteInt = 9999;
		}
		if(avgReadInt > 9999){
			avgReadInt = 9999;
		}
		if(processCount > 99){
			processCount = 99;
		}

		snprintf(output, CHARS_PER_LINE + 1, "Application Uptime: %02dh:%02dm:%02ds   ", 
			appTimeHour, appTimeMinute, appTimeSecond);
		snprintf(temp, CHARS_PER_LINE + 1, "System Uptime: %02dd:%02dh:%02dm:%02ds", 
			uptimeDay, uptimeHour, uptimeMinute, uptimeSecond);
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "Characters Received: %06d              "
			"Characters Sent: %06d", readCounter, writeCounter);
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "Average Read Time: %04d", avgReadInt);
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "%.2f", avgReadDec);
		// move the array left 1 to get rid of the zero
		// i.e. 0.25 -> .25
		for(i = 0; i < 4; i++){
			temp[i] = temp[i+1];
		}
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "s        Average Write Time: %04d", avgWriteInt);
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "%.2f", avgWriteDec);
		for(i = 0; i < 4; i++){
			temp[i] = temp[i+1];
		}
		strcat(output, temp);
		snprintf(temp, CHARS_PER_LINE + 1, "usProcess Count: %02d     Total RAM: %03.2fMB     "
			"Free RAM: %03.2fMB", processCount, totalRAM, freeRAM);
		strcat(output, temp);
		printString(output);

	}
	return NULL;
}
void printString(char str[CHARS_PER_LINE * 4 + 1]){
	struct Params params;
	int length = strlen(str);
	int i;
	params.x = 0;
	params.y = SCREEN_PARTITION;
	params.imageWidth = ASCII_SHEET_WIDTH;
	params.imageHeight = ASCII_SHEET_HEIGHT;
	for(i = 0; i < length; i++){
		// print each character without alpha blending (false)
		printChar(params, str[i], false);
		params.x += CHAR_WIDTH;
		if(str[i] == '\n' || params.x >= SCREEN_WIDTH){
			params.y += CHAR_HEIGHT;
			params.x = 0;
		}
		// something went wrong, but we don't want to have a seg fault
		if(params.y >= Y_MAX){
			break;
		}
	}
}
