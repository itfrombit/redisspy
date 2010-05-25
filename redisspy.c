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

#include <curses.h>
#include "hiredis.h"

#define REDIS_MAX_HOST_LEN	128
#define REDIS_MAX_TYPE_LEN	8
#define REDIS_MAX_KEY_LEN	64
#define REDIS_MAX_VALUE_LEN	2048

typedef struct
{
	char	type[REDIS_MAX_TYPE_LEN];
	char	key[REDIS_MAX_KEY_LEN];
	int		length;
	char	value[REDIS_MAX_VALUE_LEN];
} REDISDATA;


// Curses data
#define MAXROWS	1000
#define MAXCOLS	500

static unsigned int	screenRows;
static unsigned int	screenCols;
static WINDOW*		screen;
static unsigned int displayRows;

static REDISDATA*	redisData;
static unsigned int	redisRows;

static unsigned int	currentRow = 0;
static unsigned int	currentCol = 0;
static unsigned int	startIndex = 0;

static char			host[REDIS_MAX_HOST_LEN];
static unsigned int	port;

static const int sortByKey		= 1;
static const int sortByType		= 2;
static const int sortByLength	= 3;
static const int sortByValue	= 4;

static int sortBy;
static int sortReverse;

// Sort functions


#define SWAPIFREVERSESORT(x, y) \
	const void* tmp; \
	if (sortReverse) \
	{ \
		tmp = y; \
		y = x; \
		x = tmp; \
	} 

int compareKeys(const void* a, const void* b)
{
	SWAPIFREVERSESORT(a, b);

	return strcmp(((REDISDATA*)a)->key, ((REDISDATA*)b)->key);
}

int compareTypes(const void* a, const void* b)
{
	SWAPIFREVERSESORT(a, b);

	int r = strcmp(((REDISDATA*)a)->type, ((REDISDATA*)b)->type);

	if (r == 0)
		return compareKeys(a, b);
	
	return r;
}

int compareLength(const void* a, const void* b)
{
	SWAPIFREVERSESORT(a, b);

	int r = ((REDISDATA*)b)->length - ((REDISDATA*)a)->length;

	if (r == 0)
		return compareKeys(a, b);
	
	return r;
}

int compareValues(const void* a, const void* b)
{
	SWAPIFREVERSESORT(a, b);

	int r = strcmp(((REDISDATA*)a)->value, ((REDISDATA*)b)->value);

	if (r == 0)
		return compareKeys(a, b);
	
	return r;
}

// Screen drawing
void redisplay()
{
	unsigned int i = 0;
	unsigned int redisIndex = startIndex;

	getmaxyx(screen, screenRows, screenCols);
	clear();

	displayRows = screenRows - 3; // Title, Status, Command

	attron(A_STANDOUT);
	//attron(A_BOLD);
	//attron(A_REVERSE);
	//attron(A_UNDERLINE);
	mvaddstr(0, 0, 
	  "Key                   Type    Length  Value                                   ");

	attroff(A_STANDOUT);
	//attroff(A_BOLD);
	//attroff(A_REVERSE);
	//attroff(A_UNDERLINE);

	while ((i < displayRows) && (redisIndex < redisRows))
	{
		char line[MAXCOLS];
		sprintf(line, "%-20s  %-6s  %6d  %-40s",
				redisData[redisIndex].key,
				redisData[redisIndex].type,
				redisData[redisIndex].length,
				redisData[redisIndex].value);

		mvaddstr(i+1, 0, line); // skip the header row
		++i;
		++redisIndex;
	}

	char status[MAXCOLS];
	sprintf(status, "[host=%s:%d] [keys=%d] [%d%%] [clients=%d] [mem=%s]", 
			host, 
			port, 
			redisRows, 
			redisIndex*100/redisRows,
			5, 
			"1.5MB");

	for (i = strlen(status) - 1; i < screenCols - 1; i++)
		status[i+1] = ' ';
	status[screenCols] = '\0';

	attron(A_STANDOUT);
	mvaddstr(screenRows - 2, 0, status);
	attroff(A_STANDOUT);

	move(screenRows - 1, 0);

	refresh();
}

// Curses functions
void initCurses()
{
	screen = initscr();
	cbreak();
	noecho();
	getmaxyx(screen, screenRows, screenCols);
	clear();
	refresh();
}

void quitCurses()
{
	endwin();
}


void highlightCurrentRow()
{
	attron(A_BOLD);
	mvaddstr(currentRow, 0, redisData[currentRow].key);
	attroff(A_BOLD);
	refresh();
}

void pageup()
{
	if (((int)startIndex - (int)displayRows) < 0)
	{
		startIndex = 0;
		beep();
	}
	else
	{
		startIndex -= displayRows;
	}

	redisplay();
}

void pagedown()
{
	if ((startIndex + displayRows) > redisRows)
	{
		beep();
	}
	else
	{
		startIndex += displayRows;
	}

	redisplay();
}


void help()
{
	char line[MAXCOLS];
	strcpy(line, "r=refresh f,b=page k,t,l,v=sort q=quit");
	for (unsigned int i = strlen(line); i < screenCols; i++)
		strcat(line, " ");

	attron(A_STANDOUT);
	mvaddstr(screenRows - 1, 0, line);
	attroff(A_STANDOUT);
}

// Redis functions
int redisRefresh(char* pattern)
{
	int fd = -1;
	redisReply* r = NULL;

	r = redisConnect(&fd, host, port);
	if (r != NULL)
	{
		return -1;
	}

	r = redisCommand(fd, "KEYS %s", pattern);
	if (r->type == REDIS_REPLY_ARRAY)
	{
		// If our current buffer is big enough, don't bother
		// freeing and mallocing a new one every time.
		if (redisData && (r->elements > redisRows))
		{
			free(redisData);
			redisData = NULL;
		}

		if (redisData == NULL)
			redisData = malloc(r->elements * sizeof(REDISDATA));

		redisRows = r->elements;

		for (unsigned i = 0; i < r->elements; i++)
		{
			strcpy(redisData[i].key, r->element[i]->reply);
			redisData[i].length = 0;
			redisData[i].value[0] = '\0';

			redisReply* t = redisCommand(fd, "TYPE %s", 
			                             r->element[i]->reply);

			strcpy(redisData[i].type, t->reply);

			if (strcmp(t->reply, "string") == 0)
			{
				redisReply* v = redisCommand(fd, "GET %s", 
				                             r->element[i]->reply);

				strcpy(redisData[i].value, v->reply);
				redisData[i].length = strlen(v->reply);
				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "list") == 0)
			{
				redisReply* v = redisCommand(fd, "LRANGE %s 0 -1", 
				                             r->element[i]->reply);

				redisData[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(redisData[i].value, " ");

					strcat(redisData[i].value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "hash") == 0)
			{
				redisReply* v = redisCommand(fd, "HGETALL %s", r->element[i]->reply);
				redisData[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j+=2)
				{
					if (j > 0)
						strcat(redisData[i].value, " ");

					strcat(redisData[i].value, v->element[j]->reply);
					strcat(redisData[i].value, "->");
					/*
					if (strcmp(v->element[j+1]->reply, "") == 0)
						strcat(value, "(nil)");
					else
						strcat(value, v->element[j+1]->reply);
					*/
					strcat(redisData[i].value, v->element[j+1]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "set") == 0)
			{
				redisReply* v = redisCommand(fd, "SMEMBERS %s", r->element[i]->reply);
				redisData[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(redisData[i].value, " ");

					strcat(redisData[i].value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "zset") == 0)
			{
				redisReply* v = redisCommand(fd, "ZRANGE %s 0 -1", r->element[i]->reply);
				redisData[i].length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(redisData[i].value, " ");

					strcat(redisData[i].value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}

			freeReplyObject(t);
		}
	}

	return 0;
}


// Driver
int main(int argc, char* argv[])
{
	int done = 0;

	strcpy(host, "127.0.0.1");
	port = 6379;

	initCurses();

	redisRefresh("*");

	sortBy = sortByKey;
	sortReverse = 0;
	qsort(redisData, redisRows, sizeof(REDISDATA), compareKeys);

	redisplay();

	while (!done)
	{
		char d = getch();

		switch(d)
		{
			case 'q':
				done = 1;
				break;

			case 'p':
				break;

			case 'a':
				break;

			case 'd':
				redisplay();
				break;

			case 'r':
				redisRefresh("*");
				redisplay();
				break;

			case 'k':
				if (sortBy == sortByKey)
					sortReverse = ~sortReverse;
				else
				{
					sortBy = sortByKey;
					sortReverse = 0;
				}
				
				qsort(redisData, redisRows, sizeof(REDISDATA), compareKeys);
				redisplay();
				break;

			case 't':
				if (sortBy == sortByType)
					sortReverse = ~sortReverse;
				else
				{
					sortBy = sortByType;
					sortReverse = 0;
				}

				qsort(redisData, redisRows, sizeof(REDISDATA), compareTypes);
				redisplay();
				break;

			case 'l':
				if (sortBy == sortByLength)
					sortReverse = ~sortReverse;
				else
				{
					sortBy = sortByLength;
					sortReverse = 0;
				}

				qsort(redisData, redisRows, sizeof(REDISDATA), compareLength);
				redisplay();
				break;

			case 'v':
				if (sortBy == sortByValue)
					sortReverse = ~sortReverse;
				else
				{
					sortBy = sortByValue;
					sortReverse = 0;
				}

				qsort(redisData, redisRows, sizeof(REDISDATA), compareValues);
				redisplay();
				break;


			case 'f':
			case ' ':
				pagedown();
				break;

			case 'b':
				pageup();
				break;

			case '?':
				help();
				break;
		}
	}

	quitCurses();
}
