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

#include <ncurses.h>
#include <signal.h>

#include "hiredis.h"


// UNUSED macro via Martin Pool
#ifdef UNUSED 
#elif defined(__GNUC__) 
# define UNUSED(x) UNUSED_ ## x __attribute__((unused)) 
#elif defined(__LCLINT__) 
# define UNUSED(x) /*@unused@*/ x 
#else 
# define UNUSED(x) x 
#endif


// Declare control characters in their mnemonic form
//	e.g.: CTRL('f') = 6
#define CTRL(char) (char - 'a' + 1)

// Max values for string buffers
#define REDISSPY_MAX_HOST_LEN			128
#define REDISSPY_MAX_TYPE_LEN			8
#define REDISSPY_MAX_KEY_LEN			64
#define REDISSPY_MAX_PATTERN_LEN		REDISSPY_MAX_KEY_LEN
#define REDISSPY_MAX_VALUE_LEN			2048
#define REDISSPY_MAX_COMMAND_LEN		256
#define REDISSPY_MAX_SERVER_REPLY_LEN	2048

// Curses data
#define REDISSPY_MAX_SCREEN_ROWS	1000
#define REDISSPY_MAX_SCREEN_COLS	500

#define REDISSPY_HEADER_ROWS			1
#define REDISSPY_FOOTER_ROWS			2

#define REDISSPY_MIN_KEY_FIELD_WIDTH	16

// Command line defaults
#define REDISSPY_DEFAULT_HOST			"127.0.0.1"
#define REDISSPY_DEFAULT_PORT			6379
#define REDISSPY_DEFAULT_FILTER_PATTERN	"*"

#define sortByKey		1
#define sortByType		2
#define sortByLength	3
#define sortByValue		4


typedef struct
{
	char	type[REDISSPY_MAX_TYPE_LEN];
	char	key[REDISSPY_MAX_KEY_LEN];
	int		length;
	char	value[REDISSPY_MAX_VALUE_LEN];
} REDISDATA;

typedef struct
{
	REDISDATA*		data;
	unsigned int	keyCount;
	unsigned int	longestKeyLength;

	char			pattern[REDISSPY_MAX_PATTERN_LEN];

	int				infoConnectedClients;
	char			infoUsedMemoryHuman[32];

	int				sortBy;
	int				sortReverse;

	int				refreshInterval;

	char			host[REDISSPY_MAX_HOST_LEN];
	unsigned int	port;
	redisContext*	context;
} REDIS;

REDIS* g_redis;


typedef struct
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

	char			lastCommand[REDISSPY_MAX_COMMAND_LEN];
} REDISSPY_WINDOW;

REDISSPY_WINDOW* g_redisSpyWindow;



// Sort functions
#define SWAPIFREVERSESORT(thunk, x, y) \
	const void* tmp; \
	if (((REDIS*)thunk)->sortReverse) \
	{ \
		tmp = y; \
		y = x; \
		x = tmp; \
	} 

#define safestrcat(d, s) \
	strncat(d, s, sizeof(d) - strlen(s) - 1);

// qsort_r differs in calling conventions between
// // Linux and DARWIN/BSD.
#if defined(DARWIN) || defined(BSD)

#define DECLARE_COMPARE_FN(fn, thunk, a, b) \
	int fn(void* thunk, const void* a, const void* b)
#define CALL_COMPARE_FN(fn, thunk, a, b) \
	fn(thunk, a, b)
typedef int (*COMPARE_FN)(void* thunk, const void* a, const void* b);

#else

#define DECLARE_COMPARE_FN(fn, thunk, a, b) \
	int fn(const void* a, const void* b, void* thunk)
#define CALL_COMPARE_FN(fn, thunk, a, b) \
	fn(a, b, thunk)
typedef int (*COMPARE_FN)(const void* a, const void* b, void* thunk);

#endif


DECLARE_COMPARE_FN(compareKeys, thunk, a, b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	return strcmp(((REDISDATA*)a)->key, ((REDISDATA*)b)->key);
}


DECLARE_COMPARE_FN(compareTypes, thunk, a, b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = strcmp(((REDISDATA*)a)->type, ((REDISDATA*)b)->type);

	if (r == 0)
		return CALL_COMPARE_FN(compareKeys, thunk, a, b);

	return r;
}


DECLARE_COMPARE_FN(compareLengths, thunk, a, b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = ((REDISDATA*)b)->length - ((REDISDATA*)a)->length;

	if (r == 0)
		return CALL_COMPARE_FN(compareKeys, thunk, a, b);

	return r;
}


DECLARE_COMPARE_FN(compareValues, thunk, a, b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = strcmp(((REDISDATA*)a)->value, ((REDISDATA*)b)->value);

	if (r == 0)
		return CALL_COMPARE_FN(compareKeys, thunk, a, b);

	return r;
}


// Curses functions
void redisSpySetBusySignal(REDISSPY_WINDOW* w, int isBusy)
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


void redisSpySetRowText(REDISSPY_WINDOW* w, int row, int attr, char* text)
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


void redisSpySetHeaderLineText(REDISSPY_WINDOW* w, char* text)
{
	redisSpySetRowText(w, w->headerRow, A_STANDOUT, text);
}


void redisSpySetCommandLineText(REDISSPY_WINDOW* w, char* text)
{
	redisSpySetRowText(w, w->commandRow, 0, text);
	wmove(w->window, w->currentRow, w->currentColumn);
}


void redisSpySetStatusLineText(REDISSPY_WINDOW* w, char* text)
{
	redisSpySetRowText(w, w->statusRow, A_STANDOUT, text);
}


int redisSpyDraw(REDISSPY_WINDOW* w, REDIS* redis)
{
	char status[REDISSPY_MAX_SCREEN_COLS];

	unsigned int redisIndex = w->startIndex;

	if (w->startIndex > redis->keyCount)
		w->startIndex = 0;

	// In case the terminal window was resized...
	getmaxyx(w->window, w->rows, w->cols);
	wclear(w->window);

	w->displayRows = w->rows - 3; // Header, Status, Command

	w->headerRow = 0;
	w->statusRow = w->rows - 2;
	w->commandRow = w->rows - 1;

	char dataFormat[32];
	char headerFormat[32];
	char headerText[REDISSPY_MAX_SCREEN_COLS];

	int keyFieldWidth = MAX(REDISSPY_MIN_KEY_FIELD_WIDTH, redis->longestKeyLength);

	sprintf(dataFormat,   "%%-%ds  %%-6s  %%6d  ", keyFieldWidth);

	sprintf(headerFormat, "%%-%ds  %%-6s  %%6s  %%s", keyFieldWidth);
	sprintf(headerText, headerFormat,
				"Key",
				"Type",
				"Length",
				"Value");

	redisSpySetHeaderLineText(w, headerText);

	unsigned int i = 0;
	while ((i < w->displayRows) && (redisIndex < redis->keyCount))
	{
		char line[REDISSPY_MAX_SCREEN_COLS];

		int len = sprintf(line, dataFormat,
				redis->data[redisIndex].key,
				redis->data[redisIndex].type,
				redis->data[redisIndex].length);

		strncat(line, redis->data[redisIndex].value, w->cols - len);

		mvwaddstr(w->window, i + REDISSPY_HEADER_ROWS, 0, line); // skip the header row
		++i;
		++redisIndex;
	}

	// In case our cursor was below the end of the new data set.
	// i instead of (i - 1) because of the header row.
	if (w->currentRow > i)
		w->currentRow = i;

	if (redis->context == NULL)
	{
		sprintf(status, "[host=%s:%d] No Connection.", 
			redis->host, 
			redis->port); 
	}
	else
	{
		sprintf(status, "[host=%s:%d] [filter=%s] [keys=%d] [%d%%] [clients=%d] [mem=%s]", 
			redis->host, 
			redis->port,
			redis->pattern,
			redis->keyCount, 
			redis->keyCount ? redisIndex*100/redis->keyCount : 0,
			redis->infoConnectedClients, 
			redis->infoUsedMemoryHuman);
	}

	redisSpySetStatusLineText(w, status);

	wmove(w->window, w->currentRow, w->currentColumn);

	wrefresh(w->window);

	return 0;
}

// Curses functions
REDISSPY_WINDOW* redisSpyWindowCreate(REDISSPY_WINDOW* parent)
{
	REDISSPY_WINDOW* w = malloc(sizeof(REDISSPY_WINDOW));

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

	w->currentRow = REDISSPY_HEADER_ROWS;
	w->currentColumn = 0;

	clear();
	wrefresh(w->window);

	return w;
}

void redisSpyWindowDelete(REDISSPY_WINDOW* w)
{
	endwin();
	free(w);
}


void redisSpyWindowDeleteChild(REDISSPY_WINDOW* w)
{
	delwin(w->window);
	free(w);
}



void redisSpyHighlightCurrentRow(REDISSPY_WINDOW* w, REDIS* redis)
{
	wattron(w->window, A_BOLD);
	mvwaddstr(w->window, w->currentRow, 0, redis->data[w->currentRow].key);
	wattroff(w->window, A_BOLD);
	wrefresh(w->window);
}


int redisSpyEventMoveDown(REDISSPY_WINDOW* w, REDIS* redis)
{
	if (   (w->currentRow < w->displayRows)
	    && ((w->startIndex + w->currentRow) < redis->keyCount))
	{
		w->currentRow++;
	}
	else if (   (w->currentRow >= w->displayRows)
			 && ((w->startIndex + w->currentRow) < redis->keyCount))
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

	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventMoveUp(REDISSPY_WINDOW* w, REDIS* redis)
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

	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventPageDown(REDISSPY_WINDOW* w, REDIS* redis)
{
	if ((w->startIndex + w->displayRows) > redis->keyCount - 1)
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
	
	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventPageUp(REDISSPY_WINDOW* w, REDIS* redis)
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
	
	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventTop(REDISSPY_WINDOW* w, REDIS* redis)
{
	w->startIndex = 0;
	w->currentRow = 1;
	w->currentColumn = 0;

	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventBottom(REDISSPY_WINDOW* w, REDIS* redis)
{
	int i = redis->keyCount - w->displayRows;

	if (i < 0)
		i = 0;

	w->startIndex = i;

	if (redis->keyCount < w->displayRows)
		w->currentRow = redis->keyCount;
	else
		w->currentRow = w->displayRows;

	w->currentColumn = 0;

	redisSpyDraw(w, redis);

	return 0;
}


int redisSpyEventHelp(REDISSPY_WINDOW* w, REDIS* UNUSED(redis))
{
	//redisSpySetCommandLineText(w, "r=refresh f,b=page k,t,l,v=sort q=quit");

	redisSpySetCommandLineText(w, "q=quit");

	REDISSPY_WINDOW* dw = redisSpyWindowCreate(w);

	char headerText[REDISSPY_MAX_SCREEN_COLS];
	char statusText[REDISSPY_MAX_SCREEN_COLS];

	snprintf(headerText, REDISSPY_MAX_SCREEN_COLS,
			"RedisSpy Help");

	snprintf(statusText, REDISSPY_MAX_SCREEN_COLS,
			"Press q to return");

	redisSpySetHeaderLineText(dw, headerText);
	redisSpySetStatusLineText(dw, statusText);


	char displayRow[REDISSPY_MAX_SCREEN_COLS];

	for (unsigned int i = 0; i < MIN(r->elements/2, dw->displayRows); i++)
	{
		snprintf(displayRow, REDISSPY_MAX_SCREEN_COLS,
			"%s  ->  %s",
			r->element[2*i]->str,
			r->element[2*i+1]->str);

		mvwaddstr(dw->window, i + REDISSPY_HEADER_ROWS, 0,
				  displayRow);
	}

	wrefresh(dw->window);

	int done = 0;

	while (!done)
	{
		char c = wgetch(w->window);

		switch (c)
		{
			case 'q':
			case 27:
				redisSpyWindowDeleteChild(dw);
				done = 1;
				break;

			default:
				beep();
		}
	}

	redisSpyEventRefresh(w, redis);

	return 0;
}

// Redis functions

int redisSpyConnect(REDIS* r, char* host, unsigned int port)
{
	if (   (strncmp(r->host, host, sizeof(r->host)) == 0)
		&& (r->port == port)
		&& (r->context != NULL))
	{
		// Have a connection. Make sure it is still alive.
		redisReply* reply = redisCommand(r->context, "PING");

		if (   reply
			&& reply->type == REDIS_REPLY_STATUS
			&& (strcasecmp(reply->str, "pong") == 0))
		{
			// Good to go with cached connection
			freeReplyObject(reply);
			return 0;
		}

		if (reply)
			freeReplyObject(reply);
	}

	// Create a new connection
	redisContext* context = redisConnect(host, port);

	strncpy(r->host, host, sizeof(r->host));
	r->port = port;

	if (context->err)
	{
		r->context = NULL;
		redisFree(context);
		return -1;
	}

	r->context = context;

	return 0;
}

int redisSpyServerClearCache(REDIS* redis)
{
	if (redis == NULL)
		return -1;

	free(redis->data);
	redis->data = NULL;

	redis->keyCount = 0;
	redis->longestKeyLength = 0;

	return 0;
}

int redisSpyServerRefresh(REDIS* redis)
{
	redisReply* r = NULL;

	int ret = redisSpyConnect(redis, redis->host, redis->port);
	if (ret)
	{
		return -1;
	}

	redis->infoConnectedClients = 0;
	redis->infoUsedMemoryHuman[0] = '\0';

	r = redisCommand(redis->context, "INFO");
	if (r->type == REDIS_REPLY_STRING)
	{
		char*	c = strstr(r->str, "connected_clients");
		char*	m = strstr(r->str, "used_memory_human");
		char*	t = NULL;

		if (c)
		{
			t = strtok(c, ":\r\n");
			t = strtok(NULL, ":\r\n");

			if (t)
				redis->infoConnectedClients = atoi(t);
		}

		if (m)
		{
			t = strtok(m, ":\r\n");
			t = strtok(NULL, ":\r\n");

			if (t)
				strcpy(redis->infoUsedMemoryHuman, t);
		}
	}


	if (redis->pattern[0] == '\0')
	{
		strcpy(redis->pattern, "*");
	}


	r = redisCommand(redis->context, "KEYS %s", redis->pattern);
	if (r->type == REDIS_REPLY_ARRAY)
	{
		// If our current buffer is big enough, don't bother
		// freeing and mallocing a new one every time.
		if (redis->data && (r->elements > redis->keyCount))
		{
			free(redis->data);
			redis->data = NULL;
		}

		if (redis->data == NULL)
			redis->data = malloc(r->elements * sizeof(REDISDATA));

		redis->keyCount = r->elements;
		redis->longestKeyLength = 0;

		for (unsigned i = 0; i < r->elements; i++)
		{
			strncpy(redis->data[i].key, r->element[i]->str, sizeof(redis->data[i].key) - 1);
			redis->data[i].length = 0;
			redis->data[i].value[0] = '\0';

			unsigned int keyLength = strlen(r->element[i]->str);
			if (keyLength > redis->longestKeyLength)
				redis->longestKeyLength = keyLength;

			redisReply* t = redisCommand(redis->context, "TYPE %s", 
			                             r->element[i]->str);

			strncpy(redis->data[i].type, t->str, sizeof(redis->data[i].type) - 1);

			redisReply* v = NULL;

			if (strcmp(t->str, "string") == 0)
			{
				v = redisCommand(redis->context, "GET %s", r->element[i]->str);

				strncpy(redis->data[i].value, v->str, sizeof(redis->data[i].value) - 1);
				redis->data[i].length = strlen(v->str);
			}
			else if (strcmp(t->str, "list") == 0)
			{
				v = redisCommand(redis->context, "LRANGE %s 0 -1", r->element[i]->str);

				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->str);
				}
			}
			else if (strcmp(t->str, "hash") == 0)
			{
				v = redisCommand(redis->context, "HGETALL %s", r->element[i]->str);

				redis->data[i].length = v->elements >> 1;

				for (unsigned j = 0; j < v->elements; j+=2)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->str);
					safestrcat(redis->data[i].value, "->");
					/*
					if (strcmp(v->element[j+1]->str, "") == 0)
						safestrcat(value, "(nil)");
					else
						safestrcat(value, v->element[j+1]->str);
					*/
					safestrcat(redis->data[i].value, v->element[j+1]->str);
				}
			}
			else if (strcmp(t->str, "set") == 0)
			{
				v = redisCommand(redis->context, "SMEMBERS %s", r->element[i]->str);
				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->str);
				}
			}
			else if (strcmp(t->str, "zset") == 0)
			{
				v = redisCommand(redis->context, "ZRANGE %s 0 -1", r->element[i]->str);
				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->str);
				}
			}

			freeReplyObject(v);
			freeReplyObject(t);
		}
	}

	return 0;
}


void redisSpySort(REDIS* redis, int newSortBy)
{
	// 0 means repeat what we did last time
	if (newSortBy > 0) 
	{
		if (redis->sortBy == newSortBy)
			redis->sortReverse = ~redis->sortReverse;
		else
		{
			redis->sortReverse = 0;
			redis->sortBy = newSortBy;
		}
	}

	COMPARE_FN compareFunction = NULL;

	switch (redis->sortBy)
	{
		case sortByKey:
			compareFunction = compareKeys;
			break;

		case sortByType:
			compareFunction = compareTypes;
			break;

		case sortByLength:
			compareFunction = compareLengths;
			break;

		case sortByValue:
			compareFunction = compareValues;
			break;

		default:
			break;
	}

	if (compareFunction != NULL)
	{
#if defined(DARWIN) || defined(BSD)
		qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), redis, compareFunction);
#else
		qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), compareFunction, redis);
#endif
	}
}

// Driver

int redisSpyEventRefresh(REDISSPY_WINDOW* window, REDIS* redis)
{
	redisSpySetBusySignal(window, 1);
	redisSpyServerRefresh(redis);
	redisSpySetBusySignal(window, 0);
	redisSpySort(redis, 0);
	redisSpyDraw(window, redis);

	return 0;
}


void timerExpired(int UNUSED(i))
{
	redisSpyEventRefresh(g_redisSpyWindow, g_redis);

/*
	// Don't have to reset signal handler each time
	signal(SIGALRM, timerExpired);

	struct itimerval timer;
	setTimerInterval(&timer);
	setitimer(ITIMER_REAL, &timer, NULL);
*/
}

void redisSpyResetTimer(REDIS* redis, int interval)
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


void usage()
{
	printf("usage: redisspy [-h <host>] [-p <port>] [-k <pattern>] [-a <interval>]\n");
	printf("                [-o] [-u] [-d<delimiter>]\n");
	printf("\n");
	printf("    -h : Specify host. Default is localhost.\n");
	printf("    -p : Specify port. Default is 6379.\n");
	printf("    -k : Specify key pattern. Default is '*' (all keys).\n");
	printf("    -a : Refresh every <interval> seconds. Default is manual refresh.\n");
	printf("\n");
	printf("  redisspy can also run in non-interactive mode.\n");
	printf("    -o : output formatted dump of keys/values to stdout and exit\n");
	printf("	-u : output delimited dump of keys/values to stdout and exit\n");
	printf("    -d : change the output delimiter to <delimiter>. Default is '|'\n");
}


redisReply* redisSpyGetServerResponse(REDIS* redis, char* command)
{
	redisReply* r = NULL;

	signal(SIGALRM, SIG_IGN);

	int ret = redisSpyConnect(redis, redis->host, redis->port);

	if (ret)
	{
		return NULL;
	}

	r = redisCommand(redis->context, command);

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return r;
}

int redisSpySendCommandToServer(REDIS* redis, char* command, char* reply, int maxReplyLen)
{
	redisReply* r = NULL;

	signal(SIGALRM, SIG_IGN);

	int ret = redisSpyConnect(redis, redis->host, redis->port);
	if (ret)
	{
		strncpy(reply, "Could connect to server.", maxReplyLen - 1);

		if (redis->refreshInterval)
			signal(SIGALRM, timerExpired);

		return -1;
	}

	r = redisCommand(redis->context, command);

	if (r)
	{
		switch (r->type)
		{
			case REDIS_REPLY_STRING:
			case REDIS_REPLY_ERROR:
				strncpy(reply, r->str, maxReplyLen - 1);
				break;

			case REDIS_REPLY_INTEGER:
				snprintf(reply, maxReplyLen - 1, "%lld", r->integer);
				break;

			case REDIS_REPLY_ARRAY:
				snprintf(reply, maxReplyLen - 1, "%zu", r->elements);
				break;

			default:
				strncpy(reply, "OK", maxReplyLen - 1);
				break;
		}

		freeReplyObject(r);
	}

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return 0;
}

int redisSpyGetCommand(REDISSPY_WINDOW* w, REDIS* redis, char* prompt, char* str, int max)
{
	char	command[REDISSPY_MAX_COMMAND_LEN];

	int		done = 0;
	int		cancelled = 0;

	signal(SIGALRM, SIG_IGN);

	memset(command, 0, sizeof(command));

	redisSpySetCommandLineText(w, prompt);

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
				if (isprint(c) && (idx < REDISSPY_MAX_COMMAND_LEN))
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

	// Restore the command line
	//noecho();

	redisSpySetCommandLineText(w, "");

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return cancelled;
}


///////////////////////////////////////////////////////////////////////
//
// Event Handlers
//

int redisSpyEventQuit(REDISSPY_WINDOW* window, REDIS* UNUSED(redis))
{
	redisSpyWindowDelete(window);
	exit(0);
}


int redisSpyEventConnectToHost(REDISSPY_WINDOW* window, REDIS* redis)
{
	char hostPrompt[REDISSPY_MAX_COMMAND_LEN];
	char hostBuffer[REDISSPY_MAX_COMMAND_LEN];
	char portBuffer[REDISSPY_MAX_COMMAND_LEN];
	unsigned int port;

	sprintf(hostPrompt, "Host: (Default is %s): ", redis->host);

	if (redisSpyGetCommand(window, redis, 
				hostPrompt,
				hostBuffer, sizeof(hostBuffer)) == 0)
	{
		if (redisSpyGetCommand(window, redis,
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

			redisSpyEventRefresh(window, redis);
		}
	}

	return 0;
}


int redisSpyEventCommand(REDISSPY_WINDOW* window, REDIS* redis)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	if (redisSpyGetCommand(window, redis, 
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

		redisSpyEventRefresh(window, redis);

		redisSpySetCommandLineText(window, serverReply);
	}

	return 0;
}


int redisSpyEventRepeatCommand(REDISSPY_WINDOW* window, REDIS* redis)
{
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	if (strlen(window->lastCommand) > 0)
	{
		redisSpySendCommandToServer(redis, 
					window->lastCommand, 
					serverReply, sizeof(serverReply));

		redisSpyEventRefresh(window, redis);

		redisSpySetCommandLineText(window, serverReply);
	}

	return 0;
}


int redisSpyEventBatchFile(REDISSPY_WINDOW* window, REDIS* redis)
{
	char filename[PATH_MAX];

	if (redisSpyGetCommand(window, redis,
				"File: ",
				filename, sizeof(filename)) == 0)
	{
		FILE* fp;

		fp = fopen(filename, "r");
		if (fp == NULL)
		{
			redisSpySetCommandLineText(window, "File not found.");
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

		redisSpyEventRefresh(window, redis);

		redisSpySetCommandLineText(window, "File processed.");
	}
	return 0;
}


int redisSpyGetCurrentKeyIndex(REDISSPY_WINDOW* w, REDIS* UNUSED(redis))
{
	// Which key are we sitting on top of?
	if ((w->currentRow < 1) || (w->currentRow > w->displayRows))
	{
		return -1;
	}

	return w->startIndex + w->currentRow - REDISSPY_HEADER_ROWS;
}

int redisSpyEventDeleteKey(REDISSPY_WINDOW* w, REDIS* redis)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	int i = redisSpyGetCurrentKeyIndex(w, redis);

	if (i < 0)
	{
		beep();
		return 0;
	}

	snprintf(serverCommand, sizeof(serverCommand),
				"DEL %s", redis->data[i].key);

	redisSpySendCommandToServer(redis, serverCommand,
					serverReply, sizeof(serverReply));

	redisSpyEventRefresh(w, redis);

	redisSpySetCommandLineText(w, serverReply);

	return 0;
}


int redisSpyEventListPop(REDISSPY_WINDOW* w, REDIS* redis, char* command)
{
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];
	char serverReply[REDISSPY_MAX_SERVER_REPLY_LEN];

	int i = redisSpyGetCurrentKeyIndex(w, redis);

	if (i < 0)
	{
		beep();
		return 0;
	}

	if (strcmp(redis->data[i].type, "list") != 0)
	{
		beep();
		redisSpySetCommandLineText(w, "Not a list.");
		return 0;
	}

	snprintf(serverCommand, sizeof(serverCommand),
				"%s %s", command, redis->data[i].key);

	redisSpySendCommandToServer(redis, serverCommand,
					serverReply, sizeof(serverReply));

	redisSpyEventRefresh(w, redis);

	redisSpySetCommandLineText(w, serverReply);

	return 0;
}


int redisSpyEventListLeftPop(REDISSPY_WINDOW* w, REDIS* redis)
{
	return redisSpyEventListPop(w, redis, "LPOP");
}


int redisSpyEventListRightPop(REDISSPY_WINDOW* w, REDIS* redis)
{
	return redisSpyEventListPop(w, redis, "RPOP");
}


int redisSpyEventViewDetails(REDISSPY_WINDOW* w, REDIS* redis)
{
	int index = redisSpyGetCurrentKeyIndex(w, redis);

	if (index < 0)
	{
		beep();
		return 0;
	}

	REDISSPY_WINDOW* dw = redisSpyWindowCreate(w);

	char headerText[REDISSPY_MAX_SCREEN_COLS];
	char statusText[REDISSPY_MAX_SCREEN_COLS];

	snprintf(headerText, REDISSPY_MAX_SCREEN_COLS,
			"Key Details: %s",
			redis->data[index].key);

	snprintf(statusText, REDISSPY_MAX_SCREEN_COLS,
			"[type=%s]  [len=%d]",
			redis->data[index].type,
			redis->data[index].length);

	redisSpySetHeaderLineText(dw, headerText);
	redisSpySetStatusLineText(dw, statusText);

	redisReply* r = NULL;
	char serverCommand[REDISSPY_MAX_COMMAND_LEN];

	if (strcmp(redis->data[index].type, "string") == 0)
	{
		snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
				"GET %s", redis->data[index].key);

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			mvwaddstr(dw->window, REDISSPY_HEADER_ROWS, 0,
					  r->str);
		}
	}
	else if (   (strcmp(redis->data[index].type, "list") == 0)
			 || (strcmp(redis->data[index].type, "set") == 0)
			 || (strcmp(redis->data[index].type, "zset") == 0))
	{
		if (strcmp(redis->data[index].type, "list") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"LRANGE %s 0 -1", redis->data[index].key);
		}
		else if (strcmp(redis->data[index].type, "set") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"SMEMBERS %s", redis->data[index].key);
		}
		else if (strcmp(redis->data[index].type, "zset") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"ZRANGE %s 0 -1", redis->data[index].key);
		}

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			for (unsigned int i = 0; i < MIN(r->elements, dw->displayRows); i++)
			{
				mvwaddstr(dw->window, i + REDISSPY_HEADER_ROWS, 0,
						  r->element[i]->str);
			}
		}
	}
	else if (strcmp(redis->data[index].type, "hash") == 0)
	{
		snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
				 "HGETALL %s", redis->data[index].key);

		r = redisSpyGetServerResponse(redis, serverCommand);

		if (r)
		{
			char displayRow[REDISSPY_MAX_SCREEN_COLS];

			for (unsigned int i = 0; i < MIN(r->elements/2, dw->displayRows); i++)
			{
				snprintf(displayRow, REDISSPY_MAX_SCREEN_COLS,
					"%s  ->  %s",
					r->element[2*i]->str,
					r->element[2*i+1]->str);

				mvwaddstr(dw->window, i + REDISSPY_HEADER_ROWS, 0,
						  displayRow);
			}
		}
	}
	else
	{
		mvwaddstr(dw->window, 1, 0, "Unsupported Type.");
	}

	if (r)
		freeReplyObject(r);

	wrefresh(dw->window);

	int done = 0;

	while (!done)
	{
		char c = wgetch(w->window);

		switch (c)
		{
			case 'q':
			case 27:
				redisSpyWindowDeleteChild(dw);
				done = 1;
				break;

			default:
				beep();
		}
	}

	redisSpyEventRefresh(w, redis);

	return 0;
}


int redisSpyEventFilterKeys(REDISSPY_WINDOW* window, REDIS* redis)
{
	// Change key filter pattern
	if (redisSpyGetCommand(window, redis, 
				"Pattern: ", 
				redis->pattern, sizeof(redis->pattern)) == 0)
	{
		redisSpySetBusySignal(window, 1);
		redisSpyServerRefresh(redis);
		redisSpySetBusySignal(window, 0);
		redisSpySort(redis, 0);

		window->startIndex = 0;
		window->currentRow = 1;
		window->currentColumn = 0;

		redisSpyDraw(window, redis);
	}

	return 0;
}


int redisSpyEventAutoRefresh(REDISSPY_WINDOW* window, REDIS* redis)
{
	char refreshIntervalBuffer[80];
	redis->refreshInterval = 0;

	// Auto-refresh
	if (redisSpyGetCommand(window, redis, 
				"Refresh Interval (0 to turn off): ", 
				refreshIntervalBuffer, 
				sizeof(refreshIntervalBuffer)) == 0)
	{
		int interval = atoi(refreshIntervalBuffer);
		redisSpyResetTimer(redis, interval);
	}

	return 0;
}



// Sorting commands:
// Repeated presses will toggle asc/desc
//  s - sort by key
//  t - type
//  l - length
//  v - value
int redisSpyEventSortByKey(REDISSPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByKey);
	redisSpyDraw(window, redis);

	return 0;
}


int redisSpyEventSortByType(REDISSPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByType);
	redisSpyDraw(window, redis);

	return 0;
}


int redisSpyEventSortByLength(REDISSPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByLength);
	redisSpyDraw(window, redis);

	return 0;
}


int redisSpyEventSortByValue(REDISSPY_WINDOW* window, REDIS* redis)
{
	redisSpySort(redis, sortByValue);
	redisSpyDraw(window, redis);

	return 0;
}


typedef struct 
{
	int			key;
	const char*	desc;
	int	(*handler)(REDISSPY_WINDOW* w, REDIS* redis);
} REDIS_DISPATCH;

static REDIS_DISPATCH g_dispatchTable[] = 
{
	{ KEY_RESIZE,		"resize", redisSpyDraw },

	{ 'h',				"connect to host", redisSpyEventConnectToHost },

	{ 'd',				"DEL - delete current key", redisSpyEventDeleteKey },

	{ '[',				"LPOP - left pop current list",  redisSpyEventListLeftPop },
	{ ']',				"RPOP - right pop current list", redisSpyEventListRightPop },

	{ 'o',				"view details", redisSpyEventViewDetails },
	{ CTRL('j'),		"view details", redisSpyEventViewDetails },

	{ 'q',				"quit", redisSpyEventQuit },

	{ 'r',				"refresh", redisSpyEventRefresh },
	{ 'a',				"auto-refresh", redisSpyEventAutoRefresh },

	{ ':',				"command line",        redisSpyEventCommand },
	{ '.',				"repeat last command", redisSpyEventRepeatCommand },
	{ 'b',				"run batch file",      redisSpyEventBatchFile },

	{ 'f',				"filter keys",         redisSpyEventFilterKeys },

	{ 's',				"sort by key",         redisSpyEventSortByKey },
	{ 't',				"sort by type",        redisSpyEventSortByType },
	{ 'l',				"sort by length",      redisSpyEventSortByLength },
	{ 'v',				"sort by value",       redisSpyEventSortByValue },

	{ 'j',				"move down",           redisSpyEventMoveDown },
	{ KEY_DOWN,			"move down",           redisSpyEventMoveDown },

	{ 'k',				"move up",             redisSpyEventMoveUp },
	{ KEY_UP,			"move up",             redisSpyEventMoveUp },

	{ CTRL('f'),		"page down",           redisSpyEventPageDown },
	{ ' ',				"page down",           redisSpyEventPageDown },

	{ CTRL('b'),		"page up",             redisSpyEventPageUp },

	{ '^',				"goto top",            redisSpyEventTop },
	{ '$',				"goto bottom",         redisSpyEventBottom },
	{ 'G',				"goto bottom",         redisSpyEventBottom },

	{ '?',				"help",                redisSpyEventHelp }
};

static unsigned int g_dispatchTableSize = sizeof(g_dispatchTable)/sizeof(REDIS_DISPATCH);


int redisSpyDispatchCommand(int command, REDISSPY_WINDOW* w, REDIS* r)
{
	// Naive dispatching. 
	for (unsigned int i = 0; i < g_dispatchTableSize; i++)
	{
		if (g_dispatchTable[i].key == command)
			return (*(g_dispatchTable[i].handler))(w, r);
	}

	return -1;
}

int redisSpyEventLoop(REDISSPY_WINDOW* w, REDIS* redis)
{
	while (1)
	{
		int key = wgetch(w->window);

		// try to dispatch
		if (redisSpyDispatchCommand(key, w, redis))
		{
			redisSpySetCommandLineText(w,
				"Unknown command");
			wmove(w->window, w->currentRow, w->currentColumn);
			beep();
		}
	}

	return 0;
}


void redisSpyDump(REDIS* redis, char* delimiter, int unaligned)
{
	int r = redisSpyServerRefresh(redis);

	if (r != 0)
	{
		fprintf(stderr, "Could not connect to redis server: %s:%d",
				redis->host, redis->port);
		return;
	}

	for (unsigned int i = 0; i < redis->keyCount; i++)
	{
		if (unaligned)
		{
			printf("%s%s%s%s%d%s%s\n",
					redis->data[i].key,
					delimiter,
					redis->data[i].type,
					delimiter,
					redis->data[i].length,
					delimiter,
					redis->data[i].value);
		}
		else
		{
			printf("%-20s  %-6s  %5d  %s\n",
					redis->data[i].key,
					redis->data[i].type,
					redis->data[i].length,
					redis->data[i].value);
		}
	}
}

int redisSpyGetOptions(int argc, char* argv[], REDIS* redis)
{
	strcpy(redis->host, REDISSPY_DEFAULT_HOST);
	redis->port = REDISSPY_DEFAULT_PORT;
	strcpy(redis->pattern, REDISSPY_DEFAULT_FILTER_PATTERN);

	int dump = 0;
	int unaligned = 0;
	char delimiter[8];
	strcpy(delimiter, "|"); // default

	int c; 
	while ((c = getopt(argc, argv, "h:p:a:k:?oud:")) != -1)
	{
		switch (c)
		{
			case 'h':
				strcpy(redis->host, optarg);
				break;

			case 'p':
				redis->port = atoi(optarg);
				break;

			case 'k':
				strcpy(redis->pattern, optarg);
				break;

			case 'a':
				redis->refreshInterval = atoi(optarg);
				break;

			// The o,u,d options replace redisdump
			case 'o':
				dump = 1;
				break;

			case 'u':
				unaligned = 1;
				break;

			case 'd':
				strncpy(delimiter, optarg, sizeof(delimiter) - 1);
				break;

			case '?':
			default:
				usage();
				exit(1);
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if (dump)
	{
		redisSpyServerRefresh(redis);
		redisSpyDump(redis, delimiter, unaligned);
		exit(0);
	}

	return 0;
}


int main(int argc, char* argv[])
{
	g_redis = malloc(sizeof(REDIS));

	redisSpyGetOptions(argc, argv, g_redis);

	g_redisSpyWindow = redisSpyWindowCreate(NULL);

	// Do initial manual refresh
	// Set Reverse on so it toggles back to ascending
	g_redis->sortReverse = 0;
	g_redis->sortBy = sortByKey;

	redisSpyEventRefresh(g_redisSpyWindow, g_redis);

//	redisSpySetBusySignal(g_redisSpyWindow, 1);
//	redisSpyServerRefresh(g_redis);
//	redisSpySetBusySignal(g_redisSpyWindow, 0);
//
//	redisSpySort(g_redis, sortByKey);
//	redisSpyDraw(g_redisSpyWindow, g_redis);

	if (g_redis->refreshInterval)
	{
		// Start autorefresh
		redisSpyResetTimer(g_redis, g_redis->refreshInterval);
	}

	redisSpyEventLoop(g_redisSpyWindow, g_redis);

	return 0;
}

// eof

