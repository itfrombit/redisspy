#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ctype.h>

#include <signal.h>

#include "hiredis.h"

#include "spymodel.h"


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


redisReply* redisSpyGetServerResponse(REDIS* redis, char* command)
{
	redisReply* r = NULL;

	void (*oldHandler)(int) = signal(SIGALRM, SIG_IGN);

	int ret = redisSpyConnect(redis, redis->host, redis->port);

	if (ret)
	{
		return NULL;
	}

	r = redisCommand(redis->context, command);

	if (redis->refreshInterval)
		signal(SIGALRM, oldHandler);

	return r;
}

int redisSpySendCommandToServer(REDIS* redis, char* command, char* reply, int maxReplyLen)
{
	redisReply* r = NULL;

	void (*oldHandler)(int) = signal(SIGALRM, SIG_IGN);

	int ret = redisSpyConnect(redis, redis->host, redis->port);
	if (ret)
	{
		strncpy(reply, "Could connect to server.", maxReplyLen - 1);

		if (redis->refreshInterval)
			signal(SIGALRM, oldHandler);

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
		signal(SIGALRM, oldHandler);

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

