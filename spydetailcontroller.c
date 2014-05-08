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

#include "spydetailcontroller.h"

static SPY_WINDOW* g_redisSpyDetailWindow;

static REDIS* g_redisDetail;
static REDISDATA*	g_redisDetailData;

static SPY_WINDOW_DELEGATE* g_spyDetailWindowDelegate;


static int REDIS_SPY_DISPATCH_COMMAND_QUIT = -999999;

int spyDetailControllerEventHelp(SPY_WINDOW* w, REDIS* UNUSED(redis))
{
	spyWindowSetCommandLineText(w, "r=refresh q=quit ?=help");

	return 0;
}


// Driver

int spyDetailControllerEventRefresh(SPY_WINDOW* window, REDIS* redis)
{
	spyWindowSetBusySignal(window, 1);
	redisSpyServerRefreshKey(redis, g_redisDetailData);
	spyWindowSetBusySignal(window, 0);
	spyWindowDraw(window);

	return 0;
}

void detailTimerExpired(int UNUSED(i))
{
	spyDetailControllerEventRefresh(g_redisSpyDetailWindow, g_redisDetail);
}

void spyDetailControllerResetTimer(REDIS* redis, int interval)
{
	if (interval == 0)
	{
		// Turn off the timer
		signal(SIGALRM, SIG_IGN);
	}
	else
	{
		signal(SIGALRM, detailTimerExpired);
	}

	redis->refreshInterval = interval;

	struct itimerval timer;
	timer.it_interval.tv_sec = redis->refreshInterval;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = redis->refreshInterval;
	timer.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &timer, NULL);
}


int spyDetailControllerGetCommand(SPY_WINDOW* w, REDIS* redis, const char* prompt, char* str, int max)
{
	signal(SIGALRM, SIG_IGN);

	int result = spyWindowGetCommand(w, prompt, str, max);

	if (redis->refreshInterval)
		signal(SIGALRM, detailTimerExpired);

	return result;
}


///////////////////////////////////////////////////////////////////////
//
// Event Handlers
//

int spyDetailControllerEventQuit(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	signal(SIGALRM, SIG_IGN);

	spyWindowDelete(window);

	return REDIS_SPY_DISPATCH_COMMAND_QUIT;
}

int spyDetailControllerEventCommand(SPY_WINDOW* window, REDIS* redis)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	if (spyDetailControllerGetCommand(window, redis, 
				"Command: ", 
				serverCommand, sizeof(serverCommand)) == 0)
	{
		if (serverCommand[0] == '\0')
		{
			beep();
			return 0;
		}

		redisSpySendCommandToServer(redis, serverCommand, 
					serverReply, sizeof(serverReply));

		spyDetailControllerEventRefresh(window, redis);

		spyWindowSetCommandLineText(window, serverReply);
	}

	return 0;
}


int spyDetailControllerEventRepeatCommand(SPY_WINDOW* window, REDIS* redis)
{
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];
	char lastCommand[REDISSPY_MAX_COMMAND_LEN];

	spyWindowGetLastCommand(window, lastCommand, sizeof(lastCommand));

	if (strlen(lastCommand) > 0)
	{
		redisSpySendCommandToServer(redis, 
					lastCommand, 
					serverReply, sizeof(serverReply));

		spyDetailControllerEventRefresh(window, redis);

		spyWindowSetCommandLineText(window, serverReply);
	}

	return 0;
}


int spyDetailControllerEventDeleteKey(SPY_WINDOW* w, REDIS* redis)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	int i = spyWindowGetCurrentRow(w);

	if (i < 0)
	{
		beep();
		return 0;
	}

	snprintf(serverCommand, sizeof(serverCommand),
				"DEL %s", redis->data[i].key);

	redisSpySendCommandToServer(redis, serverCommand,
					serverReply, sizeof(serverReply));

	spyDetailControllerEventRefresh(w, redis);

	spyWindowSetCommandLineText(w, serverReply);

	return 0;
}


int spyDetailControllerEventListPop(SPY_WINDOW* w, REDIS* redis, const char* command)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	int i = spyWindowGetCurrentRow(w);

	if (i < 0)
	{
		beep();
		return 0;
	}

	if (strcmp(redis->data[i].type, "list") != 0)
	{
		beep();
		spyWindowSetCommandLineText(w, "Not a list.");
		return 0;
	}

	snprintf(serverCommand, sizeof(serverCommand),
				"%s %s", command, redis->data[i].key);

	redisSpySendCommandToServer(redis, serverCommand,
					serverReply, sizeof(serverReply));

	spyDetailControllerEventRefresh(w, redis);

	spyWindowSetCommandLineText(w, serverReply);

	return 0;
}


int spyDetailControllerEventListLeftPop(SPY_WINDOW* w, REDIS* redis)
{
	return spyDetailControllerEventListPop(w, redis, "LPOP");
}


int spyDetailControllerEventListRightPop(SPY_WINDOW* w, REDIS* redis)
{
	return spyDetailControllerEventListPop(w, redis, "RPOP");
}

/* TODO jsb */
int spyDetailControllerEventViewDetails(SPY_WINDOW* w, REDIS* redis)
{
	signal(SIGALRM, SIG_IGN);

	int index = spyWindowGetCurrentRow(w);

	if (index < 0)
	{
		beep();
		return 0;
	}

	redisReply* r = NULL;
	char serverCommand[SPY_WINDOW_MAX_COMMAND_LEN];

	if (strcmp(redis->data[index].type, "string") == 0)
	{
		snprintf(serverCommand, SPY_WINDOW_MAX_COMMAND_LEN,
				"GET %s", redis->data[index].key);

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			mvwaddstr(w->window, SPY_WINDOW_HEADER_ROWS, 0,
					  r->str);
		}
	}
	else if (   (strcmp(redis->data[index].type, "list") == 0)
			 || (strcmp(redis->data[index].type, "set") == 0)
			 || (strcmp(redis->data[index].type, "zset") == 0))
	{
		if (strcmp(redis->data[index].type, "list") == 0)
		{
			snprintf(serverCommand, SPY_WINDOW_MAX_COMMAND_LEN,
					"LRANGE %s 0 -1", redis->data[index].key);
		}
		else if (strcmp(redis->data[index].type, "set") == 0)
		{
			snprintf(serverCommand, SPY_WINDOW_MAX_COMMAND_LEN,
					"SMEMBERS %s", redis->data[index].key);
		}
		else if (strcmp(redis->data[index].type, "zset") == 0)
		{
			snprintf(serverCommand, SPY_WINDOW_MAX_COMMAND_LEN,
					"ZRANGE %s 0 -1", redis->data[index].key);
		}

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			for (unsigned int i = 0; i < MIN(r->elements, w->displayRows); i++)
			{
				mvwaddstr(w->window, i + SPY_WINDOW_HEADER_ROWS, 0,
						  r->element[i]->str);
			}
		}
	}
	else if (strcmp(redis->data[index].type, "hash") == 0)
	{
		snprintf(serverCommand, SPY_WINDOW_MAX_COMMAND_LEN,
				 "HGETALL %s", redis->data[index].key);

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			char displayRow[SPY_WINDOW_MAX_SCREEN_COLS];

			for (unsigned int i = 0; i < MIN(r->elements/2, w->displayRows); i++)
			{
				snprintf(displayRow, SPY_WINDOW_MAX_SCREEN_COLS,
					"%s  ->  %s",
					r->element[2*i]->str,
					r->element[2*i+1]->str);

				mvwaddstr(w->window, i + SPY_WINDOW_HEADER_ROWS, 0,
						  displayRow);
			}
		}
	}
	else
	{
		mvwaddstr(w->window, 1, 0, "Unsupported Type.");
	}

	if (r)
		freeReplyObject(r);

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

	spyDetailControllerEventRefresh(w, redis);

	if (redis->refreshInterval)
		signal(SIGALRM, detailTimerExpired);

	return 0;
}


int spyDetailControllerEventAutoRefresh(SPY_WINDOW* window, REDIS* redis)
{
	char refreshIntervalBuffer[80];
	redis->refreshInterval = 0;

	// Auto-refresh
	if (spyDetailControllerGetCommand(window, redis, 
				"Detail Refresh Interval (0 to turn off): ", 
				refreshIntervalBuffer, 
				sizeof(refreshIntervalBuffer)) == 0)
	{
		int interval = atoi(refreshIntervalBuffer);
		spyDetailControllerResetTimer(redis, interval);
	}

	return 0;
}


int spyDetailControllerRedraw(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowDraw(window);

	return 0;
}

int spyDetailControllerEventMoveDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveDown(window);
	return 0;
}

int spyDetailControllerEventMoveUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveUp(window);
	return 0;
}

int spyDetailControllerEventPageDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageDown(window);
	return 0;
}

int spyDetailControllerEventPageUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageUp(window);
	return 0;
}

int spyDetailControllerEventMoveToTop(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToTop(window);
	return 0;
}

int spyDetailControllerEventMoveToBottom(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToBottom(window);
	return 0;
}

typedef struct 
{
	int		key;
	int		(*handler)(SPY_WINDOW* w, REDIS* redis);
} SPY_DISPATCH;

static SPY_DISPATCH g_detailDispatchTable[] = 
{
	{ KEY_RESIZE,		spyDetailControllerRedraw },

	{ 'd',				spyDetailControllerEventDeleteKey },

	{ '[',				spyDetailControllerEventListLeftPop },
	{ ']',				spyDetailControllerEventListRightPop },

	{ 'q',				spyDetailControllerEventQuit },

	{ 'r',				spyDetailControllerEventRefresh },
	{ 'a',				spyDetailControllerEventAutoRefresh },

	{ ':',				spyDetailControllerEventCommand },
	{ '.',				spyDetailControllerEventRepeatCommand },

	{ 'j',				spyDetailControllerEventMoveDown },
	{ KEY_DOWN,			spyDetailControllerEventMoveDown },

	{ 'k',				spyDetailControllerEventMoveUp },
	{ KEY_UP,			spyDetailControllerEventMoveUp },

	{ CTRL('f'),		spyDetailControllerEventPageDown },
	{ ' ',				spyDetailControllerEventPageDown },

	{ CTRL('b'),		spyDetailControllerEventPageUp },

	{ '^',				spyDetailControllerEventMoveToTop },
	{ '$',				spyDetailControllerEventMoveToBottom },
	{ 'G',				spyDetailControllerEventMoveToBottom },

	{ '?',				spyDetailControllerEventHelp }
};

static unsigned int g_detailDispatchTableSize = sizeof(g_detailDispatchTable)/sizeof(SPY_DISPATCH);


int spyDetailControllerDispatchCommand(int command, SPY_WINDOW* w, REDIS* r)
{
	// Naive dispatching. 
	for (unsigned int i = 0; i < g_detailDispatchTableSize; i++)
	{
		if (g_detailDispatchTable[i].key == command)
			return (*(g_detailDispatchTable[i].handler))(w, r);
	}

	return -1;
}


///////////////////////////////////////////////////////////////////////
//
// Spy Window Delegate Methods
//
unsigned int spyDetailWindowDelegateRowCount(void* UNUSED(delegate))
{
	return redisSpyDetailElementCount(g_redisDetailData);
}

int spyDetailWindowDelegateValueForRow(void* UNUSED(delegate), int row, char* buffer, unsigned int bufferSize)
{
	redisSpyDetailElementAtIndex(g_redisDetailData, row, buffer, bufferSize);

	return 0;
}

int spyDetailWindowDelegateHeaderText(void* UNUSED(delegate), char* buffer, unsigned int bufferSize)
{
	snprintf(buffer, bufferSize,
			"Key Details: %s",
			g_redisDetailData->key);

	return 0;
}

int spyDetailWindowDelegateStatusText(void* UNUSED(delegate), char* buffer, unsigned int bufferSize, 
		                        unsigned int cursorIndex)
{
	snprintf(buffer, bufferSize,
			"[type=%s] [len=%d] [%d%%]",
			g_redisDetailData->type,
			g_redisDetailData->length,
			g_redisDetailData->reply->elements
				? cursorIndex * 100 / redisSpyDetailElementCount(g_redisDetailData) 
				: 100);

	return 0;
}

///////////////////////////////////////////////////////////////////////
//
// Main event loop
//
int spyDetailControllerEventLoop(SPY_WINDOW* w, REDIS* redis)
{
	spyDetailControllerEventRefresh(w, redis);

	if (redis->refreshInterval)
	{
		// Start autorefresh
		spyDetailControllerResetTimer(redis, redis->refreshInterval);
	}

	while (1)
	{
		int key = wgetch(w->window);
		int result = spyDetailControllerDispatchCommand(key, w, redis);

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


int spyDetailControllerRun(SPY_WINDOW* parent, REDIS* redis, unsigned int index)
{
	g_redisSpyDetailWindow = spyWindowCreate(parent);
	g_redisDetail = redis;
	g_redisDetailData = &redis->data[index];

	g_spyDetailWindowDelegate = spyWindowDelegateCreate(
									spyDetailWindowDelegateRowCount,
									spyDetailWindowDelegateValueForRow,
									spyDetailWindowDelegateHeaderText,
									spyDetailWindowDelegateStatusText);

	spyWindowSetDelegate(g_redisSpyDetailWindow, g_spyDetailWindowDelegate);

	spyDetailControllerEventLoop(g_redisSpyDetailWindow, redis);

	return 0;
}

