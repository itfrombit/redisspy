// Command Summary:
//   j : move down a line
//   k : move up a line
//
//   f : move forward a page (also spacebar)
//   b : move backward a page
//
//   p : filter pattern
//
//   l : redraw screen
//   r : refresh
//   a : toggle autorefresh
//
//   q : quit
//   ? : help
//

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
#include "spydetailcontroller.h"

static SPY_WINDOW* g_redisSpyWindow;
static REDIS* g_redis;
static SPY_WINDOW_DELEGATE* g_spyWindowDelegate;

static int REDIS_SPY_DISPATCH_COMMAND_QUIT = -999999;

int spyControllerEventHelp(SPY_WINDOW* w, REDIS* UNUSED(redis))
{
	spyWindowSetCommandLineText(w, "r=refresh f,b=page k,t,l,v=sort q=quit");

	return 0;
}


// Driver

int spyControllerEventRefresh(SPY_WINDOW* window, REDIS* redis)
{
	spyWindowSetBusySignal(window, 1);
	redisSpyServerRefresh(redis);
	spyWindowSetBusySignal(window, 0);
	redisSpySort(redis, 0);
	spyWindowDraw(window);

	return 0;
}

void timerExpired(int UNUSED(i))
{
	spyControllerEventRefresh(g_redisSpyWindow, g_redis);
}

void spyControllerResetTimer(REDIS* redis, int interval)
{
	if (interval == 0)
	{
		// Turn off the timer
		signal(SIGALRM, SIG_IGN);
	}
	else
	{
		signal(SIGALRM, timerExpired);
	}

	redis->refreshInterval = interval;

	struct itimerval timer;
	timer.it_interval.tv_sec = redis->refreshInterval;
	timer.it_interval.tv_usec = 0;
	timer.it_value.tv_sec = redis->refreshInterval;
	timer.it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, &timer, NULL);
}


int spyControllerGetCommand(SPY_WINDOW* w, REDIS* redis, const char* prompt, char* str, int max)
{
	signal(SIGALRM, SIG_IGN);

	int result = spyWindowGetCommand(w, prompt, str, max);

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return result;
}


///////////////////////////////////////////////////////////////////////
//
// Event Handlers
//

int spyControllerEventQuit(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowDelete(window);

	return REDIS_SPY_DISPATCH_COMMAND_QUIT;
}


int spyControllerEventConnectToHost(SPY_WINDOW* window, REDIS* redis)
{
	char hostPrompt[REDISSPY_MAX_COMMAND_LEN];
	char hostBuffer[REDISSPY_MAX_COMMAND_LEN];
	char portBuffer[REDISSPY_MAX_COMMAND_LEN];
	unsigned int port;

	sprintf(hostPrompt, "Host: (Default is %s): ", redis->host);

	if (spyControllerGetCommand(window, redis, 
				hostPrompt,
				hostBuffer, sizeof(hostBuffer)) == 0)
	{
		if (spyControllerGetCommand(window, redis,
				"Port (Default is 6379): ",
				portBuffer, sizeof(portBuffer)) == 0)
		{
			if (hostBuffer[0] == '\0')
				strncpy(hostBuffer, redis->host, sizeof(hostBuffer));

			if (portBuffer[0] == '\0')
				port = REDISSPY_DEFAULT_PORT;
			else
				port = (unsigned int)atoi(portBuffer);

			int r = redisSpyConnect(redis, hostBuffer, port);

			if (r != 0)
				redisSpyServerClearCache(redis);

			spyControllerEventRefresh(window, redis);
		}
	}

	return 0;
}


int spyControllerEventCommand(SPY_WINDOW* window, REDIS* redis)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	if (spyControllerGetCommand(window, redis, 
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

		spyControllerEventRefresh(window, redis);

		spyWindowSetCommandLineText(window, serverReply);
	}

	return 0;
}


int spyControllerEventRepeatCommand(SPY_WINDOW* window, REDIS* redis)
{
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];
	char lastCommand[REDISSPY_MAX_COMMAND_LEN];

	spyWindowGetLastCommand(window, lastCommand, sizeof(lastCommand));

	if (strlen(lastCommand) > 0)
	{
		redisSpySendCommandToServer(redis, 
					lastCommand, 
					serverReply, sizeof(serverReply));

		spyControllerEventRefresh(window, redis);

		spyWindowSetCommandLineText(window, serverReply);
	}

	return 0;
}


int spyControllerEventBatchFile(SPY_WINDOW* window, REDIS* redis)
{
	char filename[PATH_MAX];

	if (spyControllerGetCommand(window, redis,
				"File: ",
				filename, sizeof(filename)) == 0)
	{
		FILE* fp;

		fp = fopen(filename, "r");
		if (fp == NULL)
		{
			spyWindowSetCommandLineText(window, "File not found.");
			return 0;
		}
		
		char s[REDISSPY_MAX_COMMAND_LEN];
		char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

		while (fgets(s, sizeof(s), fp) != NULL)
		{
			s[strlen(s)- 1] = '\0';
			
			if (s[0] != '\0' && s[0] != '#')
				redisSpySendCommandToServer(redis, s,
						serverReply, sizeof(serverReply));
		}

		fclose(fp);

		spyControllerEventRefresh(window, redis);

		spyWindowSetCommandLineText(window, "File processed.");
	}
	return 0;
}


int spyControllerEventDeleteKey(SPY_WINDOW* w, REDIS* redis)
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

	spyControllerEventRefresh(w, redis);

	spyWindowSetCommandLineText(w, serverReply);

	return 0;
}


int spyControllerEventListPop(SPY_WINDOW* w, REDIS* redis, const char* command)
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

	spyControllerEventRefresh(w, redis);

	spyWindowSetCommandLineText(w, serverReply);

	return 0;
}


int spyControllerEventListLeftPop(SPY_WINDOW* w, REDIS* redis)
{
	return spyControllerEventListPop(w, redis, "LPOP");
}


int spyControllerEventListRightPop(SPY_WINDOW* w, REDIS* redis)
{
	return spyControllerEventListPop(w, redis, "RPOP");
}

int spyControllerEventViewDetails(SPY_WINDOW* w, REDIS* redis)
{
	signal(SIGALRM, SIG_IGN);

	int index = spyWindowGetCurrentRow(w);

	if (index < 0)
	{
		beep();
		return 0;
	}

	spyDetailControllerRun(w, redis, index);

	spyControllerEventRefresh(w, redis);

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return 0;
}


int spyControllerEventFilterKeys(SPY_WINDOW* window, REDIS* redis)
{
	// Change key filter pattern
	if (spyControllerGetCommand(window, redis, 
				"Pattern: ", 
				redis->pattern, sizeof(redis->pattern)) == 0)
	{
		spyWindowSetBusySignal(window, 1);
		redisSpyServerRefresh(redis);
		spyWindowSetBusySignal(window, 0);
		redisSpySort(redis, 0);

		spyWindowResetCursor(window);

		spyWindowDraw(window);
	}

	return 0;
}


int spyControllerEventAutoRefresh(SPY_WINDOW* window, REDIS* redis)
{
	char refreshIntervalBuffer[80];
	redis->refreshInterval = 0;

	// Auto-refresh
	if (spyControllerGetCommand(window, redis, 
				"Refresh Interval (0 to turn off): ", 
				refreshIntervalBuffer, 
				sizeof(refreshIntervalBuffer)) == 0)
	{
		int interval = atoi(refreshIntervalBuffer);
		spyControllerResetTimer(redis, interval);
	}

	return 0;
}



// Sorting commands:
// Repeated presses will toggle asc/desc
//  s - sort by key
//  t - type
//  l - length
//  v - value
int spyControllerEventSortByKey(SPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByKey);
	spyWindowDraw(window);

	return 0;
}


int spyControllerEventSortByType(SPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByType);
	spyWindowDraw(window);

	return 0;
}


int spyControllerEventSortByLength(SPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByLength);
	spyWindowDraw(window);

	return 0;
}


int spyControllerEventSortByValue(SPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByValue);
	spyWindowDraw(window);

	return 0;
}

int spyControllerRedraw(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowDraw(window);

	return 0;
}

int spyControllerEventMoveDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveDown(window);
	return 0;
}

int spyControllerEventMoveUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveUp(window);
	return 0;
}

int spyControllerEventPageDown(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageDown(window);
	return 0;
}

int spyControllerEventPageUp(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowPageUp(window);
	return 0;
}

int spyControllerEventMoveToTop(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToTop(window);
	return 0;
}

int spyControllerEventMoveToBottom(SPY_WINDOW* window, REDIS* UNUSED(redis))
{
	spyWindowMoveToBottom(window);
	return 0;
}

typedef struct 
{
	int		key;
	int		(*handler)(SPY_WINDOW* w, REDIS* redis);
} SPY_DISPATCH;

static SPY_DISPATCH g_dispatchTable[] = 
{
	{ KEY_RESIZE,		spyControllerRedraw },

	{ 'h',				spyControllerEventConnectToHost },

	{ 'd',				spyControllerEventDeleteKey },

	{ '[',				spyControllerEventListLeftPop },
	{ ']',				spyControllerEventListRightPop },

	{ 'o',				spyControllerEventViewDetails },
	{ CTRL('j'),		spyControllerEventViewDetails },

	{ 'q',				spyControllerEventQuit },

	{ 'r',				spyControllerEventRefresh },
	{ 'a',				spyControllerEventAutoRefresh },

	{ ':',				spyControllerEventCommand },
	{ '.',				spyControllerEventRepeatCommand },
	{ 'b',				spyControllerEventBatchFile },

	{ 'f',				spyControllerEventFilterKeys },

	{ 's',				spyControllerEventSortByKey },
	{ 't',				spyControllerEventSortByType },
	{ 'l',				spyControllerEventSortByLength },
	{ 'v',				spyControllerEventSortByValue },

	{ 'j',				spyControllerEventMoveDown },
	{ KEY_DOWN,			spyControllerEventMoveDown },

	{ 'k',				spyControllerEventMoveUp },
	{ KEY_UP,			spyControllerEventMoveUp },

	{ CTRL('f'),		spyControllerEventPageDown },
	{ ' ',				spyControllerEventPageDown },

	{ CTRL('b'),		spyControllerEventPageUp },

	{ '^',				spyControllerEventMoveToTop },
	{ '$',				spyControllerEventMoveToBottom },
	{ 'G',				spyControllerEventMoveToBottom },

	{ '?',				spyControllerEventHelp }
};

static unsigned int g_dispatchTableSize = sizeof(g_dispatchTable)/sizeof(SPY_DISPATCH);


int redisSpyDispatchCommand(int command, SPY_WINDOW* w, REDIS* r)
{
	// Naive dispatching. 
	for (unsigned int i = 0; i < g_dispatchTableSize; i++)
	{
		if (g_dispatchTable[i].key == command)
			return (*(g_dispatchTable[i].handler))(w, r);
	}

	return -1;
}


///////////////////////////////////////////////////////////////////////
//
// Spy Window Delegate Methods
//
unsigned int spyWindowDelegateRowCount(void* UNUSED(delegate))
{
	//SPY_CONTROLLER* self = (SPY_CONTROLLER*)delegate;
	return g_redis->keyCount;
}

int spyWindowDelegateValueForRow(void* UNUSED(delegate), int row, char* buffer, unsigned int bufferSize)
{
	char format[64];
	int keyFieldWidth = MAX(SPY_WINDOW_MIN_KEY_FIELD_WIDTH, g_redis->longestKeyLength);
	sprintf(format, "%%-%ds  %%-6s  %%6d  ", keyFieldWidth);

	int len = snprintf(buffer, bufferSize, format,
					   g_redis->data[row].key,
					   g_redis->data[row].type,
					   g_redis->data[row].length);

	strncat(buffer, g_redis->data[row].value, bufferSize - len);

	return 0;
}

int spyWindowDelegateHeaderText(void* UNUSED(delegate), char* buffer, unsigned int bufferSize)
{
	char format[64];
	int keyFieldWidth = MAX(SPY_WINDOW_MIN_KEY_FIELD_WIDTH, g_redis->longestKeyLength);

	sprintf(format, "%%-%ds  %%-6s  %%6s  %%s", keyFieldWidth);
	snprintf(buffer, bufferSize, format, "Key", "Type", "Length", "Value");

	return 0;
}

int spyWindowDelegateStatusText(void* UNUSED(delegate), char* buffer, unsigned int bufferSize, 
		                        unsigned int cursorIndex)
{
	if (g_redis->context == NULL)
	{
		snprintf(buffer, bufferSize,
				 "[host=%s:%d] No Connection.", 
				 g_redis->host, 
				 g_redis->port); 
	}
	else
	{
		snprintf(buffer, bufferSize,
				 "[host=%s:%d] [filter=%s] [keys=%d] [%d%%] [clients=%d] [mem=%s]", 
				 g_redis->host, 
				 g_redis->port,
				 g_redis->pattern,
				 g_redis->keyCount, 
				 g_redis->keyCount ? cursorIndex*100/g_redis->keyCount : 0,
				 g_redis->infoConnectedClients, 
				 g_redis->infoUsedMemoryHuman);
	}

	return 0;
}

///////////////////////////////////////////////////////////////////////
//
// Main event loop
//
int spyControllerEventLoop(SPY_WINDOW* w, REDIS* redis)
{
	g_redisSpyWindow = w;
	g_redis = redis;

	g_spyWindowDelegate = spyWindowDelegateCreate(
								spyWindowDelegateRowCount,
								spyWindowDelegateValueForRow,
								spyWindowDelegateHeaderText,
								spyWindowDelegateStatusText);

	spyWindowSetDelegate(w, g_spyWindowDelegate);

    // Do initial manual refresh
	// Set Reverse on so it toggles back to ascending
	redis->sortReverse = 0;
	redis->sortBy = sortByKey;

	spyControllerEventRefresh(w, redis);

	if (redis->refreshInterval)
	{
		// Start autorefresh
		spyControllerResetTimer(redis, redis->refreshInterval);
	}

	while (1)
	{
		int key = wgetch(w->window);
		int result = redisSpyDispatchCommand(key, w, redis);

		if (result == REDIS_SPY_DISPATCH_COMMAND_QUIT)
		{
			return 0;
		}
		else if (result != 0)
		{
			spyWindowSetCommandLineText(w,
				"Unknown command");
			wmove(w->window, w->currentRow, w->currentColumn);
			beep();
		}
	}

	return 0;
}

