#include "hiredis.h"
#include "spyutils.h"

// Max values for string buffers
#define REDISSPY_MAX_HOST_LEN			128
#define REDISSPY_MAX_TYPE_LEN			8
#define REDISSPY_MAX_KEY_LEN			64
#define REDISSPY_MAX_PATTERN_LEN		REDISSPY_MAX_KEY_LEN
#define REDISSPY_MAX_VALUE_LEN			2048
#define REDISSPY_MAX_COMMAND_LEN		256
#define REDISSPY_MAX_SERVER_REPLY_LEN	2048

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

	// For detail items
	redisReply*	reply;

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


// Sort functions
#define SWAPIFREVERSESORT(thunk, x, y) \
	const void* tmp; \
	if (((REDIS*)thunk)->sortReverse) \
	{ \
		tmp = y; \
		y = x; \
		x = tmp; \
	} 


DECLARE_COMPARE_FN(compareKeys, thunk, a, b);
DECLARE_COMPARE_FN(compareTypes, thunk, a, b);
DECLARE_COMPARE_FN(compareLengths, thunk, a, b);
DECLARE_COMPARE_FN(compareValues, thunk, a, b);

// Redis functions

REDIS* redisSpyCreate();
void redisSpyDelete(REDIS* r);

int redisSpyConnect(REDIS* r, char* host, unsigned int port);
int redisSpyServerClearCache(REDIS* redis);
int redisSpyServerRefresh(REDIS* redis);
void redisSpySort(REDIS* redis, int newSortBy);
int redisSpyServerRefreshKey(REDIS* redis, REDISDATA* data);

redisReply* redisSpyGetServerResponse(REDIS* redis, char* command);
int redisSpySendCommandToServer(REDIS* redis, char* command, char* reply, int maxReplyLen);

void redisSpyDump(REDIS* redis, char* delimiter, int unaligned);

unsigned int redisSpyKeyCount(REDIS* redis);
unsigned int redisSpyLongestKeyLength(REDIS* redis);
char* redisSpyKeyAtIndex(REDIS* redis, unsigned int index);

int redisSpyDetailElementCount(REDISDATA* data);
int redisSpyDetailElementAtIndex(REDISDATA* data, unsigned int index, char* buffer, unsigned int size);


