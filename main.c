#include "spymodel.h"
#include "spywindow.h"
#include "spycontroller.h"


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
	REDIS* redis = redisSpyCreate();

	redisSpyGetOptions(argc, argv, redis);

	SPY_WINDOW* spyWindow = spyWindowCreate(NULL);

	spyControllerEventLoop(spyWindow, redis);

	redisSpyDelete(redis);

	return 0;
}


