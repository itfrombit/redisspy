#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <ctype.h>

#include <ncurses.h>
#include <signal.h>

#define SPY_WINDOW_MAX_COMMAND_LEN	256

// Curses data
#define SPY_WINDOW_MAX_SCREEN_ROWS    1000
#define SPY_WINDOW_MAX_SCREEN_COLS    500

#define SPY_WINDOW_HEADER_ROWS            1
#define SPY_WINDOW_FOOTER_ROWS            2

#define SPY_WINDOW_MIN_KEY_FIELD_WIDTH	16


typedef struct _spy_window_delegate
{
	unsigned int (*fpRowCount)(void* self);
	int (*fpValueForRow)(void* self, int row, char* buffer, unsigned int bufferSize);
	int (*fpHeaderText)(void* self, char* buffer, unsigned int bufferSize);
	int (*fpStatusText)(void* self, char* buffer, unsigned int bufferSize, unsigned int cursorIndex);

} SPY_WINDOW_DELEGATE;


typedef struct _spy_window
{
	WINDOW*			window;

	unsigned int	rows;
	unsigned int	cols;

	unsigned int	displayRows;

	unsigned int	headerRow;
	unsigned int	statusRow;
	unsigned int	commandRow;

	unsigned int	startIndex;

	unsigned int	currentRow;
	unsigned int	currentColumn;

	char			lastCommand[SPY_WINDOW_MAX_COMMAND_LEN];

	SPY_WINDOW_DELEGATE*	delegate;

} SPY_WINDOW;


SPY_WINDOW* spyWindowCreate(SPY_WINDOW* parent);
void spyWindowDelete(SPY_WINDOW* w);

void spyWindowSetDelegate(SPY_WINDOW* w, SPY_WINDOW_DELEGATE* delegate);

// Curses functions
void spyWindowSetBusySignal(SPY_WINDOW* w, int isBusy);
void spyWindowSetRowText(SPY_WINDOW* w, int row, int attr, char* text);
int spyWindowGetCurrentRow(SPY_WINDOW* w);

void spyWindowSetHeaderLineText(SPY_WINDOW* w, char* text);
void spyWindowSetCommandLineText(SPY_WINDOW* w, char* text);
void spyWindowSetStatusLineText(SPY_WINDOW* w, char* text);

int spyWindowGetCommand(SPY_WINDOW* w, char* prompt, char* command, int max);
int spyWindowGetLastCommand(SPY_WINDOW* w, char* command, int max);

int spyWindowDraw(SPY_WINDOW* w);

void spyWindowDeleteChild(SPY_WINDOW* w);
void spyWindowHighlightCurrentRow(SPY_WINDOW* w, char* key);

int spyWindowMoveDown(SPY_WINDOW* w);
int spyWindowMoveUp(SPY_WINDOW* w);

int spyWindowPageDown(SPY_WINDOW* w);
int spyWindowPageUp(SPY_WINDOW* w);

int spyWindowMoveToTop(SPY_WINDOW* w);
int spyWindowMoveToBottom(SPY_WINDOW* w);

void spyWindowResetCursor(SPY_WINDOW* w);


