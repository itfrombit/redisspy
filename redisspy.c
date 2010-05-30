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


#define REDIS_MAX_HOST_LEN		128
#define REDIS_MAX_TYPE_LEN		8
#define REDIS_MAX_KEY_LEN		64
#define REDIS_MAX_PATTERN_LEN	REDIS_MAX_KEY_LEN
#define REDIS_MAX_VALUE_LEN		2048
#define REDIS_MAX_COMMAND_LEN	256

static const char*				g_defaultHost = "127.0.0.1";
static const int				g_defaultPort = 6379;
static const char*				g_defaultPattern = "*";



#define sortByKey		1
#define sortByType		2
#define sortByLength	3
#define sortByValue		4


typedef struct
{
	char	type[REDIS_MAX_TYPE_LEN];
	char	key[REDIS_MAX_KEY_LEN];
	int		length;
	char	value[REDIS_MAX_VALUE_LEN];
} REDISDATA;

typedef struct
{
	REDISDATA*		data;
	unsigned int	keyCount;

	char			pattern[REDIS_MAX_PATTERN_LEN];

	int				infoConnectedClients;
	char			infoUsedMemoryHuman[32];

	int				sortBy;
	int				sortReverse;

	int				refreshInterval;

	char			host[REDIS_MAX_HOST_LEN];
	unsigned int	port;

	int				deadServer;
} REDIS;

REDIS* g_redis;

// Curses data
#define MAXROWS	1000
#define MAXCOLS	500

typedef struct
{
	WINDOW*			window;

	unsigned int	rows;
	unsigned int	cols;

	unsigned int	displayRows;
	unsigned int	startIndex;

	unsigned int	currentRow;
	unsigned int	currentColumn;

	char			lastCommand[REDIS_MAX_COMMAND_LEN];
} REDISSPYWINDOW;

REDISSPYWINDOW* g_redisSpyWindow;



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


int compareKeys(void* thunk, const void* a, const void* b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	return strcmp(((REDISDATA*)a)->key, ((REDISDATA*)b)->key);
}

int compareTypes(void* thunk, const void* a, const void* b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = strcmp(((REDISDATA*)a)->type, ((REDISDATA*)b)->type);

	if (r == 0)
		return compareKeys(thunk, a, b);
	
	return r;
}

int compareLengths(void* thunk, const void* a, const void* b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = ((REDISDATA*)b)->length - ((REDISDATA*)a)->length;

	if (r == 0)
		return compareKeys(thunk, a, b);
	
	return r;
}

int compareValues(void* thunk, const void* a, const void* b)
{
	SWAPIFREVERSESORT(thunk, a, b);

	int r = strcmp(((REDISDATA*)a)->value, ((REDISDATA*)b)->value);

	if (r == 0)
		return compareKeys(thunk, a, b);
	
	return r;
}


// Curses functions
void redisSpySetBusySignal(REDISSPYWINDOW* w, int isBusy)
{
	// Put a busy signal on the status line
	attron(A_STANDOUT);

	if (isBusy)
		mvaddstr(w->rows - 2, w->cols - 1, "*");
	else
		mvaddstr(w->rows - 2, w->cols - 1, " ");

	attroff(A_STANDOUT);
	refresh();
}

void redisSpySetRowText(REDISSPYWINDOW* w, int row, int attr, char* text)
{
	if (attr)
		attron(attr);

	mvaddstr(row, 0, text);

	for (unsigned int i = strlen(text); i < w->cols; i++)
		addch(' ');

	if (attr)
		attroff(attr);

	refresh();
}

void redisSpySetHeaderLineText(REDISSPYWINDOW* w, char* text)
{
	redisSpySetRowText(w, 0, A_STANDOUT, text);
}

void redisSpySetCommandLineText(REDISSPYWINDOW* w, char* text)
{
	redisSpySetRowText(w, w->rows - 1, 0, text);
}

void redisSpySetStatusLineText(REDISSPYWINDOW* w, char* text)
{
	redisSpySetRowText(w, w->rows - 2, A_STANDOUT, text);
}

// Screen drawing
void redisSpyDraw(REDISSPYWINDOW* w, REDIS* redis)
{
	unsigned int i = 0;
	unsigned int redisIndex = w->startIndex;
	char status[MAXCOLS];

	// In case the terminal window was resized...
	getmaxyx(w->window, w->rows, w->cols);
	clear();

	w->displayRows = w->rows - 3; // Title, Status, Command


	redisSpySetHeaderLineText(w, "Key                   Type    Length  Value");

	i = 0;
	while ((i < w->displayRows) && (redisIndex < redis->keyCount))
	{
		char line[MAXCOLS];
		sprintf(line, "%-20s  %-6s  %6d  ",
				redis->data[redisIndex].key,
				redis->data[redisIndex].type,
				redis->data[redisIndex].length);
		strncat(line, redis->data[redisIndex].value, w->cols - 38);

		mvaddstr(i + 1, 0, line); // skip the header row
		++i;
		++redisIndex;
	}

	if (redis->deadServer)
	{
		sprintf(status, "[host=%s:%d] No Connection.", 
			redis->host, 
			redis->port); 
	}
	else
	{
		sprintf(status, "[host=%s:%d] [keys=%d] [%d%%] [clients=%d] [mem=%s]", 
			redis->host, 
			redis->port, 
			redis->keyCount, 
			redis->keyCount ? redisIndex*100/redis->keyCount : 0,
			redis->infoConnectedClients, 
			redis->infoUsedMemoryHuman);
	}

	redisSpySetStatusLineText(w, status);

	move(w->rows - 1, 0);

	refresh();
}

// Curses functions
REDISSPYWINDOW* redisSpyWindowCreate()
{
	REDISSPYWINDOW* w = malloc(sizeof(REDISSPYWINDOW));

	// Init curses
	w->window = initscr();
	cbreak();
	noecho();
	getmaxyx(w->window, w->rows, w->cols);
	clear();
	refresh();

	return w;
}

void redisSpyWindowDelete(REDISSPYWINDOW* w)
{
	endwin();

	free(w);
}



void redisSpyHighlightCurrentRow(REDISSPYWINDOW* w, REDIS* redis)
{
	attron(A_BOLD);
	mvaddstr(w->currentRow, 0, redis->data[w->currentRow].key);
	attroff(A_BOLD);
	refresh();
}

void redisSpyPageup(REDISSPYWINDOW* w, REDIS* redis)
{
	if (((int)w->startIndex - (int)w->displayRows) < 0)
	{
		w->startIndex = 0;
		beep();
	}
	else
	{
		w->startIndex -= w->displayRows;
	}

	redisSpyDraw(w, redis);
}

void redisSpyPagedown(REDISSPYWINDOW* w, REDIS* redis)
{
	if ((w->startIndex + w->displayRows) > redis->keyCount)
	{
		beep();
	}
	else
	{
		w->startIndex += w->displayRows;
	}

	redisSpyDraw(w, redis);
}


void redisSpyHelp(REDISSPYWINDOW* w)
{
	redisSpySetCommandLineText(w, "r=refresh f,b=page k,t,l,v=sort q=quit");
}

// Redis functions
int redisSpyServerRefresh(REDIS* redis)
{
	int fd = -1;
	redisReply* r = NULL;

	r = redisConnect(&fd, redis->host, redis->port);
	if (r != NULL)
	{
		redis->deadServer = 1;
		return -1;
	}

	redis->deadServer = 0;

	redis->infoConnectedClients = 0;
	strcpy(redis->infoUsedMemoryHuman, "");

	r = redisCommand(fd, "INFO");
	if (r->type == REDIS_REPLY_STRING)
	{
		char*	c = strstr(r->reply, "connected_clients");
		char*	m = strstr(r->reply, "used_memory_human");
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


	r = redisCommand(fd, "KEYS %s", redis->pattern);
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

		for (unsigned i = 0; i < r->elements; i++)
		{
			strcpy(redis->data[i].key, r->element[i]->reply);
			redis->data[i].length = 0;
			redis->data[i].value[0] = '\0';

			redisReply* t = redisCommand(fd, "TYPE %s", 
			                             r->element[i]->reply);

			strcpy(redis->data[i].type, t->reply);

			redisReply* v = NULL;

			if (strcmp(t->reply, "string") == 0)
			{
				v = redisCommand(fd, "GET %s", r->element[i]->reply);

				strcpy(redis->data[i].value, v->reply);
				redis->data[i].length = strlen(v->reply);
			}
			else if (strcmp(t->reply, "list") == 0)
			{
				v = redisCommand(fd, "LRANGE %s 0 -1", r->element[i]->reply);

				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->reply);
				}
			}
			else if (strcmp(t->reply, "hash") == 0)
			{
				v = redisCommand(fd, "HGETALL %s", r->element[i]->reply);

				redis->data[i].length = v->elements >> 1;

				for (unsigned j = 0; j < v->elements; j+=2)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->reply);
					safestrcat(redis->data[i].value, "->");
					/*
					if (strcmp(v->element[j+1]->reply, "") == 0)
						safestrcat(value, "(nil)");
					else
						safestrcat(value, v->element[j+1]->reply);
					*/
					safestrcat(redis->data[i].value, v->element[j+1]->reply);
				}
			}
			else if (strcmp(t->reply, "set") == 0)
			{
				v = redisCommand(fd, "SMEMBERS %s", r->element[i]->reply);
				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->reply);
				}
			}
			else if (strcmp(t->reply, "zset") == 0)
			{
				v = redisCommand(fd, "ZRANGE %s 0 -1", r->element[i]->reply);
				redis->data[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						safestrcat(redis->data[i].value, " ");

					safestrcat(redis->data[i].value, v->element[j]->reply);
				}
			}

			freeReplyObject(v);
			freeReplyObject(t);
		}
	}

	close(fd);

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

	switch (redis->sortBy)
	{
		case sortByKey:
			qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), redis, compareKeys);
			break;

		case sortByType:
			qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), redis, compareTypes);
			break;

		case sortByLength:
			qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), redis, compareLengths);
			break;

		case sortByValue:
			qsort_r(redis->data, redis->keyCount, sizeof(REDISDATA), redis, compareValues);
			break;
	}
}

// Driver

void timerExpired(int UNUSED(i))
{
	redisSpySetBusySignal(g_redisSpyWindow, 1);
	redisSpyServerRefresh(g_redis);
	redisSpySetBusySignal(g_redisSpyWindow, 0);

	redisSpySort(g_redis, 0);
	redisSpyDraw(g_redisSpyWindow, g_redis);

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
	printf("\n");
	printf("    -h : Specify host. Default is localhost.\n");
	printf("    -p : Specify port. Default is 6379.\n");
	printf("    -k : Specify key pattern. Default is '*' (all keys).\n");
	printf("    -a : Refresh every <interval> seconds. Default is manual refresh.\n");
	printf("\n");
}


int redisSpySendCommandToServer(REDIS* redis, char* command, char* reply, int maxReplyLen)
{
	int fd = -1;
	redisReply* r = NULL;

	signal(SIGALRM, SIG_IGN);

	r = redisConnect(&fd, redis->host, redis->port);
	if (r != NULL)
	{
		strncpy(reply, "Could connect to server.", maxReplyLen);

		if (redis->refreshInterval)
			signal(SIGALRM, timerExpired);

		return -1;
	}

	r = redisCommand(fd, command);

	if (r)
	{
		switch (r->type)
		{
			case REDIS_REPLY_STRING:
			case REDIS_REPLY_ERROR:
				strncpy(reply, r->reply, maxReplyLen);
				break;

			case REDIS_REPLY_INTEGER:
				snprintf(reply, maxReplyLen, "%lld", r->integer);
				break;

			case REDIS_REPLY_ARRAY:
				snprintf(reply, maxReplyLen, "%zu", r->elements);
				break;

			default:
				strncpy(reply, "OK", maxReplyLen);
				break;
		}

		freeReplyObject(r);
	}

	close(fd);

	if (redis->refreshInterval)
		signal(SIGALRM, timerExpired);

	return 0;
}

int redisSpyGetCommand(REDISSPYWINDOW* w, REDIS* redis, char* prompt, char* str, int max)
{
	char	command[REDIS_MAX_COMMAND_LEN];

	int		done = 0;
	int		cancelled = 0;

	signal(SIGALRM, SIG_IGN);

	memset(command, 0, REDIS_MAX_COMMAND_LEN);

	redisSpySetCommandLineText(w, prompt);

	refresh();

	int row = w->rows - 1;
	int startCol = strlen(prompt);

	int col = startCol;
	int idx = 0;

	move(row, col);

	while (!done && !cancelled)
	{
		char c = getch();

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
					move(row, col);
					delch();
				}
				break;

			case 10:
			case 13:
				if (strcmp(command, "!") == 0)
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
				if (isprint(c))
				{
					insch(c);

					col++;
					command[idx++] = c;

					move(row, col);
					refresh();
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


int main(int argc, char* argv[])
{
	g_redis = malloc(sizeof(REDIS));

	// Might hide the global redis better in the future...
	REDIS* redis = g_redis;


	strcpy(redis->host, g_defaultHost);
	redis->port = g_defaultPort;
	strcpy(redis->pattern, g_defaultPattern);

	char refreshIntervalBuffer[80];
	redis->refreshInterval = 0;

	char serverCommand[256];
	char serverReply[256];

	int c; 
	while ((c = getopt(argc, argv, "h:p:a:k:?")) != -1)
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

			case '?':
			default:
				usage();
				exit(1);
				break;
		}
	}

	argc -= optind;
	argv += optind;

	g_redisSpyWindow = redisSpyWindowCreate();

	// Do initial manual refresh
	// Set Reverse on so it toggles back to ascending
	redis->sortReverse = ~0;
	redis->sortBy = sortByKey;

	redisSpySetBusySignal(g_redisSpyWindow, 1);
	redisSpyServerRefresh(redis);
	redisSpySetBusySignal(g_redisSpyWindow, 0);

	redisSpySort(redis, sortByKey);
	redisSpyDraw(g_redisSpyWindow, redis);

	if (redis->refreshInterval)
	{
		// Start autorefresh
		redisSpyResetTimer(redis, redis->refreshInterval);
	}

	int done = 0;
	while (!done)
	{
		int d = getch();

		switch(d)
		{
			case KEY_RESIZE:
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			// Command Mode
			case ':':
				if (redisSpyGetCommand(g_redisSpyWindow, redis, "Command: ", 
							serverCommand, sizeof(serverCommand)) == 0)
				{
					//int r = 
					redisSpySendCommandToServer(redis, serverCommand, 
								serverReply, sizeof(serverReply));

					redisSpySetBusySignal(g_redisSpyWindow, 1);
					redisSpyServerRefresh(redis);
					redisSpySetBusySignal(g_redisSpyWindow, 0);
					redisSpySort(redis, 0);
					redisSpyDraw(g_redisSpyWindow, redis);

					redisSpySetCommandLineText(g_redisSpyWindow, serverReply);
				}
				break;

			// Quit
			case 'q':
				done = 1;
				break;

			// Change key filter pattern
			case 'p':
				if (redisSpyGetCommand(g_redisSpyWindow, redis, "Pattern: ", 
							redis->pattern, sizeof(redis->pattern)) == 0)
				{
					redisSpySetBusySignal(g_redisSpyWindow, 1);
					redisSpyServerRefresh(redis);
					redisSpySetBusySignal(g_redisSpyWindow, 0);
					redisSpySort(redis, 0);
					redisSpyDraw(g_redisSpyWindow, redis);
				}
				break;

			// Auto-refresh
			case 'a':
				if (redisSpyGetCommand(g_redisSpyWindow, redis, "Refresh Interval (0 to turn off): ", 
						refreshIntervalBuffer, 
						sizeof(refreshIntervalBuffer)) == 0)
				{
					int interval = atoi(refreshIntervalBuffer);
					redisSpyResetTimer(redis, interval);
				}
				break;

			// Redraw screen
			case 'd':
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			// Refresh data from server
			case 'r':
				redisSpySetBusySignal(g_redisSpyWindow, 1);
				redisSpyServerRefresh(redis);
				redisSpySetBusySignal(g_redisSpyWindow, 0);
				redisSpySort(redis, 0);
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			// Sorting commands:
			// Repeated presses will toggle asc/desc
			//  k - key
			//  t - type
			//  l - length
			//  v - value
			case 'k':
				redisSpySort(redis, sortByKey);
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			case 't':
				redisSpySort(redis, sortByType);
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			case 'l':
				redisSpySort(redis, sortByLength);
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			case 'v':
				redisSpySort(redis, sortByValue);
				redisSpyDraw(g_redisSpyWindow, redis);
				break;

			// Page down
			case 'f':
			case ' ':
				redisSpyPagedown(g_redisSpyWindow, redis);
				break;

			// Page up
			case 'b':
				redisSpyPageup(g_redisSpyWindow, redis);
				break;

			// Help
			case '?':
				redisSpyHelp(g_redisSpyWindow);
				break;
		}
	}

	redisSpyWindowDelete(g_redisSpyWindow);

	exit(0);
}
