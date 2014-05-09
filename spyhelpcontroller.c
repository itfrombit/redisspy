#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/time.h>
#include <ctype.h>

#include <signal.h>

#include "spymodel.h"
#include "spywindow.h"

#include "spycontroller.h"
#include "spyhelpcontroller.h"

static SPY_WINDOW* g_redisSpyHelpWindow;

static SPY_WINDOW_DELEGATE* g_spyHelpWindowDelegate;

static SPY_DISPATCH* g_redisDispatchTable;
static int g_redisDispatchCount;

static int REDIS_SPY_DISPATCH_COMMAND_QUIT = -999999;

int spyHelpControllerEventHelp(SPY_WINDOW* w, REDIS* UNUSED(redis))
{
	spyWindowSetCommandLineText(w, "q=quit");

	return 0;
}


///////////////////////////////////////////////////////////////////////
//
// Event Handlers
//

int spyHelpControllerEventQuit(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	signal(SIGALRM, SIG_IGN);

	spyWindowDelete(window);

	return REDIS_SPY_DISPATCH_COMMAND_QUIT;
}



int spyHelpControllerView(SPY_WINDOW* w)
{
	signal(SIGALRM, SIG_IGN);

	int index = spyWindowGetCurrentRow(w);

	if (index < 0)
	{
		beep();
		return 0;
	}

	wrefresh(w->window);

	int done = 0;

	while (!done)
	{
		char c = wgetch(w->window);

		switch (c)
		{
			case 'q':
			case 27:
				spyWindowDeleteChild(w);
				done = 1;
				break;

			default:
				beep();
		}
	}

	return 0;
}


int spyHelpControllerRedraw(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowDraw(window);

	return 0;
}

int spyHelpControllerEventMoveDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveDown(window);
	return 0;
}

int spyHelpControllerEventMoveUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveUp(window);
	return 0;
}

int spyHelpControllerEventPageDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageDown(window);
	return 0;
}

int spyHelpControllerEventPageUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageUp(window);
	return 0;
}

int spyHelpControllerEventMoveToTop(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToTop(window);
	return 0;
}

int spyHelpControllerEventMoveToBottom(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToBottom(window);
	return 0;
}

static SPY_DISPATCH g_helpDispatchTable[] = 
{
	{ KEY_RESIZE,		"redraw",           spyHelpControllerRedraw },

	{ 'q',				"quit",             spyHelpControllerEventQuit },


	{ 'j',				"move down",        spyHelpControllerEventMoveDown },
	{ KEY_DOWN,			"move down",        spyHelpControllerEventMoveDown },

	{ 'k',				"move up",          spyHelpControllerEventMoveUp },
	{ KEY_UP,			"move up",          spyHelpControllerEventMoveUp },

	{ CTRL('f'),		"page down",        spyHelpControllerEventPageDown },
	{ ' ',				"page down",        spyHelpControllerEventPageDown },

	{ CTRL('b'),		"page up",          spyHelpControllerEventPageUp },

	{ '^',				"move to top",      spyHelpControllerEventMoveToTop },
	{ '$',				"move to bottom",   spyHelpControllerEventMoveToBottom },
	{ 'G',				"move to bottom",   spyHelpControllerEventMoveToBottom },

	{ '?',				"help",             spyHelpControllerEventHelp }
};

static unsigned int g_helpDispatchTableSize = sizeof(g_helpDispatchTable)/sizeof(SPY_DISPATCH);


int spyHelpControllerDispatchCommand(int command, SPY_WINDOW* w)
{
	// Naive dispatching. 
	for (unsigned int i = 0; i < g_helpDispatchTableSize; i++)
	{
		if (g_helpDispatchTable[i].key == command)
			return (*(g_helpDispatchTable[i].handler))(w, NULL);
	}

	return -1;
}


///////////////////////////////////////////////////////////////////////
//
// Spy Window Delegate Methods
//
unsigned int spyHelpWindowDelegateRowCount(void* UNUSED(delegate))
{
	return g_redisDispatchCount;
}

int spyHelpWindowDelegateValueForRow(void* UNUSED(delegate), int row, char* buffer, unsigned int bufferSize)
{
	char keybinding[16];
	int key = g_redisDispatchTable[row].key;

	if (key == KEY_SEPARATOR)
	{
		strcpy(buffer, "");
		return 0;
	}

	if (key <= CTRL('z'))
	{
		snprintf(keybinding, sizeof(keybinding), "^%c",
				 'a' + key - 1);
	}
	else if (key == KEY_RESIZE)
	{
		strcpy(keybinding, "<resize>");
	}
	else if (key == KEY_DOWN)
	{
		strcpy(keybinding, "<down>");
	}
	else if (key == KEY_UP)
	{
		strcpy(keybinding, "<up>");
	}
	else
	{
		snprintf(keybinding, sizeof(keybinding), "%c", key);
	}

	snprintf(buffer, bufferSize, "%8s  -  %s",
	         keybinding, g_redisDispatchTable[row].desc);

	return 0;
}

int spyHelpWindowDelegateHeaderText(void* UNUSED(delegate), char* buffer, unsigned int bufferSize)
{
	snprintf(buffer, bufferSize,
			"RedisSpy Help");

	return 0;
}

int spyHelpWindowDelegateStatusText(void* UNUSED(delegate), char* buffer, unsigned int UNUSED(bufferSize), 
		                        unsigned int UNUSED(cursorIndex))
{
	strcpy(buffer, "Press q to return");

	return 0;
}

///////////////////////////////////////////////////////////////////////
//
// Main event loop
//
int spyHelpControllerEventLoop(SPY_WINDOW* w)
{
	spyWindowDraw(w);

	while (1)
	{
		int key = wgetch(w->window);
		int result = spyHelpControllerDispatchCommand(key, w);

		if (result == REDIS_SPY_DISPATCH_COMMAND_QUIT)
		{
			return 0;
		}
		else if (result != 0)
		{
			spyWindowSetCommandLineText(w,
				"Unknown command");
			spyWindowRestoreCursor(w);

			spyWindowRestoreCursor(w);
			spyWindowBeep(w);
		}
	}

	return 0;
}


int spyHelpControllerRun(SPY_WINDOW* parent, SPY_DISPATCH* dispatchTable, int dispatchCount)
{
	g_redisDispatchTable = dispatchTable;
	g_redisDispatchCount = dispatchCount;

	g_redisSpyHelpWindow = spyWindowCreate(parent);

	g_spyHelpWindowDelegate = spyWindowDelegateCreate(
									spyHelpWindowDelegateRowCount,
									spyHelpWindowDelegateValueForRow,
									spyHelpWindowDelegateHeaderText,
									spyHelpWindowDelegateStatusText);

	spyWindowSetDelegate(g_redisSpyHelpWindow, g_spyHelpWindowDelegate);

	spyHelpControllerEventLoop(g_redisSpyHelpWindow);

	return 0;
}

