#include "spywindow.h"

SPY_WINDOW_DELEGATE* spyWindowDelegateCreate(
	unsigned int (*fpRowCount)(void* self),
	int (*fpValueForRow)(void* self, int row, char* buffer, unsigned int bufferSize),
	int (*fpHeaderText)(void* self, char* buffer, unsigned int bufferSize),
	int (*fpStatusText)(void* self, char* buffer, unsigned int bufferSize, unsigned int cursorIndex))
{
	SPY_WINDOW_DELEGATE* d = malloc(sizeof(SPY_WINDOW_DELEGATE));

	d->fpRowCount = fpRowCount;
	d->fpValueForRow = fpValueForRow;
	d->fpHeaderText = fpHeaderText;
	d->fpStatusText = fpStatusText;

	return d;
}

void spyWindowDelegateDelete(SPY_WINDOW_DELEGATE* delegate)
{
	free(delegate);
}


void spyWindowSetDelegate(SPY_WINDOW* w, SPY_WINDOW_DELEGATE* delegate)
{
	w->delegate = delegate;
}

// Curses functions
void spyWindowSetBusySignal(SPY_WINDOW* w, int isBusy)
{
	// Put a busy signal on the status line
	wattron(w->window, A_STANDOUT);

	if (isBusy)
		mvwaddstr(w->window, w->statusRow, w->cols - 1, "*");
	else
		mvwaddstr(w->window, w->statusRow, w->cols - 1, " ");

	wattroff(w->window, A_STANDOUT);
	refresh();
}


void spyWindowSetRowText(SPY_WINDOW* w, int row, int attr, const char* text)
{
	if (attr)
		wattron(w->window, attr);

	mvwaddstr(w->window, row, 0, text);

	for (unsigned int i = strlen(text); i < w->cols; i++)
		waddch(w->window, ' ');

	if (attr)
		wattroff(w->window, attr);

	refresh();
}

int spyWindowGetCurrentRow(SPY_WINDOW* w)
{
	if ((w->currentRow < 1) || (w->currentRow > w->displayRows))
	{
		return -1;
	}

	return w->startIndex + w->currentRow - SPY_WINDOW_HEADER_ROWS;
}

void spyWindowSetHeaderLineText(SPY_WINDOW* w, const char* text)
{
	spyWindowSetRowText(w, w->headerRow, A_STANDOUT, text);
}


void spyWindowSetCommandLineText(SPY_WINDOW* w, const char* text)
{
	spyWindowSetRowText(w, w->commandRow, 0, text);
	wmove(w->window, w->currentRow, w->currentColumn);
}


void spyWindowSetStatusLineText(SPY_WINDOW* w, const char* text)
{
	spyWindowSetRowText(w, w->statusRow, A_STANDOUT, text);
}


int spyWindowDraw(SPY_WINDOW* w)
{
	char status[SPY_WINDOW_MAX_SCREEN_COLS];

	unsigned int redisIndex = w->startIndex;

	if (w->startIndex > w->delegate->fpRowCount(w->delegate))
		w->startIndex = 0;

	// In case the terminal window was resized...
	getmaxyx(w->window, w->rows, w->cols);
	wclear(w->window);

	w->displayRows = w->rows - 3; // Header, Status, Command

	w->headerRow = 0;
	w->statusRow = w->rows - 2;
	w->commandRow = w->rows - 1;

	char headerText[SPY_WINDOW_MAX_SCREEN_COLS];
	w->delegate->fpHeaderText(w->delegate, headerText, MIN(SPY_WINDOW_MAX_SCREEN_COLS, w->cols));
	spyWindowSetHeaderLineText(w, headerText);

	unsigned int i = 0;
	while ((i < w->displayRows) && (redisIndex < w->delegate->fpRowCount(w->delegate)))
	{
		char line[SPY_WINDOW_MAX_SCREEN_COLS];

		w->delegate->fpValueForRow(w->delegate, redisIndex, line, MIN(SPY_WINDOW_MAX_SCREEN_COLS, w->cols));

		mvwaddstr(w->window, i + SPY_WINDOW_HEADER_ROWS, 0, line); // skip the header row
		++i;
		++redisIndex;
	}

	// In case our cursor was below the end of the new data set.
	// i instead of (i - 1) because of the header row.
	if (w->currentRow > i)
		w->currentRow = i;

	w->delegate->fpStatusText(w->delegate, status, MIN(SPY_WINDOW_MAX_SCREEN_COLS, w->cols), redisIndex);

	spyWindowSetStatusLineText(w, status);

	wmove(w->window, w->currentRow, w->currentColumn);

	wrefresh(w->window);

	return 0;
}

// Curses functions
SPY_WINDOW* spyWindowCreate(SPY_WINDOW* parent)
{
	SPY_WINDOW* w = malloc(sizeof(SPY_WINDOW));

	// Init curses
	if (parent == NULL)
	{
		w->window = initscr();
		cbreak();
		noecho();
	}
	else
	{
		w->window = newwin(parent->rows, parent->cols, 0, 0);
	}

	getmaxyx(w->window, w->rows, w->cols);

	w->displayRows = w->rows - 3; // Header, Status, Command

	w->headerRow = 0;
	w->statusRow = w->rows - 2;
	w->commandRow = w->rows - 1;

	w->startIndex = 0;

	w->currentRow = SPY_WINDOW_HEADER_ROWS;
	w->currentColumn = 0;

	w->lastCommand[0] = '\0';

	clear();
	wrefresh(w->window);

	return w;
}

void spyWindowDelete(SPY_WINDOW* w)
{
	endwin();
	free(w);
}


void spyWindowDeleteChild(SPY_WINDOW* w)
{
	delwin(w->window);
	free(w);
}



void spyWindowHighlightCurrentRow(SPY_WINDOW* w, char* str)
{
	wattron(w->window, A_BOLD);
	mvwaddstr(w->window, w->currentRow, 0, str);
	wattroff(w->window, A_BOLD);
	wrefresh(w->window);
}


int spyWindowMoveDown(SPY_WINDOW* w)
{
	if (   (w->currentRow < w->displayRows)
	    && ((w->startIndex + w->currentRow) < w->delegate->fpRowCount(w->delegate)))
	{
		w->currentRow++;
	}
	else if (   (w->currentRow >= w->displayRows)
			 && ((w->startIndex + w->currentRow) < w->delegate->fpRowCount(w->delegate)))
	{
		w->startIndex++;
	}
	else
	{
		beep();
	}

	wmove(w->window,
		  w->currentRow,
	      w->currentColumn);

	spyWindowDraw(w);

	return 0;
}


int spyWindowMoveUp(SPY_WINDOW* w)
{
	if (w->currentRow > 1) 
	{
		w->currentRow--;
	}
	else if (w->startIndex > 0)
	{
		w->startIndex--;
	}
	else
	{
		beep();
	}

	wmove(w->window,
		  w->currentRow,
	      w->currentColumn);

	spyWindowDraw(w);

	return 0;
}


int spyWindowPageDown(SPY_WINDOW* w)
{
	if ((w->startIndex + w->displayRows) > w->delegate->fpRowCount(w->delegate) - 1)
	{
		beep();
		return 0;
	}
	else
	{
		w->startIndex += w->displayRows;
	}
	
	w->currentRow = 1;
	w->currentColumn = 0;
	
	spyWindowDraw(w);

	return 0;
}


int spyWindowPageUp(SPY_WINDOW* w)
{
	if (w->startIndex == 0)
	{
		beep();
		return 0;
	}
	if (((int)w->startIndex - (int)w->displayRows) < 0)
	{
		w->startIndex = 0;
	}
	else
	{
		w->startIndex -= w->displayRows;
	}

	w->currentRow = 1;
	w->currentColumn = 0;
	
	spyWindowDraw(w);

	return 0;
}


int spyWindowMoveToTop(SPY_WINDOW* w)
{
	w->startIndex = 0;
	w->currentRow = 1;
	w->currentColumn = 0;

	spyWindowDraw(w);

	return 0;
}


int spyWindowMoveToBottom(SPY_WINDOW* w)
{
	unsigned int rowCount = w->delegate->fpRowCount(w->delegate);

	int i = rowCount - w->displayRows;

	if (i < 0)
		i = 0;

	w->startIndex = i;

	if (rowCount < w->displayRows)
		w->currentRow = rowCount;
	else
		w->currentRow = w->displayRows;

	w->currentColumn = 0;

	spyWindowDraw(w);

	return 0;
}


void spyWindowResetCursor(SPY_WINDOW* w)
{
	w->startIndex = 0;
	w->currentRow = 1;
	w->currentColumn = 0;
}

void spyWindowRestoreCursor(SPY_WINDOW* w)
{
	wmove(w->window, w->currentRow, w->currentColumn);
}

void spyWindowBeep(SPY_WINDOW* UNUSED(w))
{
	beep();
}

int spyWindowGetLastCommand(SPY_WINDOW* w, char* command, int commandLength)
{
	strncpy(command, w->lastCommand, commandLength);
	return 0;
}


int spyWindowGetCommand(SPY_WINDOW* w, const char* prompt, char* str, int max)
{
	char	command[SPY_WINDOW_MAX_COMMAND_LEN];

	int		done = 0;
	int		cancelled = 0;

	memset(command, 0, sizeof(command));

	spyWindowSetCommandLineText(w, prompt);

	wrefresh(w->window);

	int row = w->rows - 1;
	int startCol = strlen(prompt);

	int col = startCol;
	int idx = 0;

	wmove(w->window, row, col);

	while (!done && !cancelled)
	{
		char c = wgetch(w->window);

		switch (c)
		{
			case 27: // Escape
				cancelled = 1;
				beep();
				break;

			case 127: // Delete
				if (idx > 0)
				{
					col--;
					idx--;
					command[idx] = 0;
					wmove(w->window, row, col);
					wdelch(w->window);
				}
				break;

			case 10:
			case 13:
				if (strcmp(command, ".") == 0)
				{
					// Return the previous command
					strncpy(str, w->lastCommand, max - 1);
				}
				else
				{
					strncpy(str, command, max - 1);
					strncpy(w->lastCommand, command, sizeof(w->lastCommand));
				}
				done = 1;
				break;

			default:
				if (isprint(c) && (idx < SPY_WINDOW_MAX_COMMAND_LEN))
				{
					winsch(w->window, c);

					col++;
					command[idx++] = c;

					wmove(w->window, row, col);
					wrefresh(w->window);
				}
				else
				{
					beep();
				}
				break;
		}
	}

	spyWindowSetCommandLineText(w, "");

	return cancelled;
}


