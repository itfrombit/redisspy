#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ctype.h>

#include <signal.h>

#include "hiredis.h"

#include "spymodel.h"

REDIS* redisSpyCreate()
{
	REDIS* r = malloc(sizeof(REDIS));

	r->data = NULL;
	r->keyCount = 0;
	r->longestKeyLength = 0;

	r->pattern[0] = '\0';
	r->infoConnectedClients = 0;
	r->infoUsedMemoryHuman[0] = '\0';

	r->sortBy = 0;
	r->sortReverse = 0;

	r->refreshInterval = r->refreshInterval;

	r->host[0] = '\0';
	r->port = 0;

	return r;
}


void redisSpyDelete(REDIS* r)
{
	free(r);
}

////////////////////////////////////////////////////////////////////////
// Accessors
// 
unsigned int redisSpyKeyCount(REDIS* r)
{
	return r->keyCount;
}

unsigned int redisSpyLongestKeyLength(REDIS* r)
{
	return r->longestKeyLength;
}

char* redisSpyKeyAtIndex(REDIS* r, unsigned int index)
{
	if (index >= r->keyCount)
		return NULL;

	return r->data[index].key;
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

#if 0
int redisSpyServerRefreshKey(REDIS* redis, REDISDATA* data)
{
	redisReply* r = NULL;

	if (data->reply)
	{
		freeReplyObject(data->reply);
		data->reply = NULL;
	}

	char serverCommand[REDISSPY_MAX_COMMAND_LEN];

	if (strcmp(data->type, "string") == 0)
	{
		snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
				"GET %s", data->key);

		r = redisSpyGetServerResponse(redis, serverCommand);
	}
	else if (   (strcmp(data->type, "list") == 0)
			 || (strcmp(data->type, "set") == 0)
			 || (strcmp(data->type, "zset") == 0))
	{
		if (strcmp(data->type, "list") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"LRANGE %s 0 -1", data->key);
		}
		else if (strcmp(data->type, "set") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"SMEMBERS %s", data->key);
		}
		else if (strcmp(data->type, "zset") == 0)
		{
			snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
					"ZRANGE %s 0 -1", data->key);
		}

		r = redisSpyGetServerResponse(redis, serverCommand);
	}
	else if (strcmp(data->type, "hash") == 0)
	{
		snprintf(serverCommand, REDISSPY_MAX_COMMAND_LEN,
				 "HGETALL %s", data->key);

		r = redisSpyGetServerResponse(redis, serverCommand);
	}
	else
	{
		// Unsupported type...
	}

	data->reply = r;

	return 0;
}
#endif

int redisSpyDetailElementCount(REDISDATA* data)
{
    if (strcmp(data->type, "string") == 0)
	{
		return 1;
	}
	else if (   (strcmp(data->type, "list") == 0)
	         || (strcmp(data->type, "set") == 0)
	         || (strcmp(data->type, "zset") == 0))
	{
		return data->reply->elements;
	}
	else if (strcmp(data->type, "hash") == 0)
	{
		return data->reply->elements/2;
	}
	else
	{
		return 1;
	}
}

int redisSpyDetailElementAtIndex(REDISDATA* data, unsigned int index, char* buffer, unsigned int size)
{
    if (strcmp(data->type, "string") == 0)
	{
		strncpy(buffer, data->reply->str, size);
	}
	else if (   (strcmp(data->type, "list") == 0)
	         || (strcmp(data->type, "set") == 0)
	         || (strcmp(data->type, "zset") == 0))
	{
		strncpy(buffer, data->reply->element[index]->str, size);
	}
	else if (strcmp(data->type, "hash") == 0)
	{
		snprintf(buffer, size, "%s  ->  %s",
				 data->reply->element[2*index]->str,
				 data->reply->element[2*index+1]->str);
	}
	else
	{
		strncpy(buffer, data->reply->str, size);
	}

	return 0;
}


int redisSpyServerRefreshKey(REDIS* redis, REDISDATA* data)
{
	data->length = 0;
	data->value[0] = '\0';

	unsigned int keyLength = strlen(data->key);
	if (keyLength > redis->longestKeyLength)
		redis->longestKeyLength = keyLength;

	redisReply* t = redisCommand(redis->context, "TYPE %s", 
	                             data->key);

	strncpy(data->type, t->str, sizeof(data->type) - 1);

	redisReply* v = NULL;

	if (strcmp(t->str, "string") == 0)
	{
		v = redisCommand(redis->context, "GET %s", data->key);

		strncpy(data->value, v->str, sizeof(data->value) - 1);
		data->length = strlen(v->str);
	}
	else if (strcmp(t->str, "list") == 0)
	{
		v = redisCommand(redis->context, "LRANGE %s 0 -1", data->key);

		data->length = v->elements;

		for (unsigned j = 0; j < v->elements; j++)
		{
			if (j > 0)
				safestrcat(data->value, " ");

			safestrcat(data->value, v->element[j]->str);
		}
	}
	else if (strcmp(t->str, "hash") == 0)
	{
		v = redisCommand(redis->context, "HGETALL %s", data->key);

		data->length = v->elements >> 1;

		for (unsigned j = 0; j < v->elements; j+=2)
		{
			if (j > 0)
				safestrcat(data->value, " ");

			safestrcat(data->value, data->key);
			safestrcat(data->value, "->");
			safestrcat(data->value, v->element[j+1]->str);
		}
	}
	else if (strcmp(t->str, "set") == 0)
	{
		v = redisCommand(redis->context, "SMEMBERS %s", data->key);
		data->length = v->elements;

		for (unsigned j = 0; j < v->elements; j++)
		{
			if (j > 0)
				safestrcat(data->value, " ");

			safestrcat(data->value, v->element[j]->str);
		}
	}
	else if (strcmp(t->str, "zset") == 0)
	{
		v = redisCommand(redis->context, "ZRANGE %s 0 -1", data->key);
		data->length = v->elements;

		for (unsigned j = 0; j < v->elements; j++)
		{
			if (j > 0)
				safestrcat(data->value, " ");

			safestrcat(data->value, v->element[j]->str);
		}
	}

	data->reply = v;

	// freeReplyObject(v);
	freeReplyObject(t);

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

	for (unsigned i = 0; i < redis->keyCount; i++)
	{
		if (redis->data[i].reply)
		{
			freeReplyObject(redis->data[i].reply);
			redis->data[i].reply = NULL;
		}
	}

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

	freeReplyObject(r);


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

			redisSpyServerRefreshKey(redis, &redis->data[i]);
		}
	}

	freeReplyObject(r);

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



