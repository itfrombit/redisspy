#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "anet.h"
#include "sds.h"
#include "hiredis.h"

#define REDIS_REPLY_MAX_LEN	1024


int redisRefresh(char* pattern)
{
	redisReply* r;
	int fd;

	r = redisConnect(&fd, "127.0.0.1", 6379);

	if (r != NULL)
	{
		printf("Connection error: %s\n", r->reply);
		exit(1);
	}

	r = redisCommand(fd, "KEYS %s", pattern);
	if (r->type == REDIS_REPLY_ARRAY)
	{
		for (unsigned i = 0; i < r->elements; i++)
		{
			char value[REDIS_REPLY_MAX_LEN];
			value[0] = '\0';
			int length = 0;

			redisReply* t = redisCommand(fd, "TYPE %s", r->element[i]->reply);

			if (strcmp(t->reply, "string") == 0)
			{
				redisReply* v = redisCommand(fd, "GET %s", r->element[i]->reply);
				strcpy(value, v->reply);
				length = strlen(value);
				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "list") == 0)
			{
				redisReply* v = redisCommand(fd, "LRANGE %s 0 -1", r->element[i]->reply);
				length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(value, " ");

					strcat(value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "hash") == 0)
			{
				redisReply* v = redisCommand(fd, "HGETALL %s", r->element[i]->reply);
				length = v->elements;

				for (unsigned j = 0; j < v->elements; j+=2)
				{
					if (j > 0)
						strcat(value, " ");

					strcat(value, v->element[j]->reply);
					strcat(value, "->");
					/*
					if (strcmp(v->element[j+1]->reply, "") == 0)
						strcat(value, "(nil)");
					else
						strcat(value, v->element[j+1]->reply);
					*/
					strcat(value, v->element[j+1]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "set") == 0)
			{
				redisReply* v = redisCommand(fd, "SMEMBERS %s", r->element[i]->reply);
				length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(value, " ");

					strcat(value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}
			else if (strcmp(t->reply, "zset") == 0)
			{
				redisReply* v = redisCommand(fd, "ZRANGE %s 0 -1", r->element[i]->reply);
				length = v->elements;

				for (unsigned j = 0; j < v->elements; j++)
				{
					if (j > 0)
						strcat(value, " ");

					strcat(value, v->element[j]->reply);
				}

				freeReplyObject(v);
			}


			printf("%4u  %-6s  %-16s  %4d  %s\n", 
					i, 
					t->reply, 
					r->element[i]->reply,
					length,
					value);

			freeReplyObject(t);
		}
	}

	close(fd);

	return 0;
}


int main(int argc, char* argv[])
{
	char* defaultPattern = "*";

	char* pattern = defaultPattern;

	if (argc > 1)
		pattern = argv[1];
	else
		pattern = "*";

	redisRefresh(pattern);
}

