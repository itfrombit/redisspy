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


DECLARE_COMPARE_FN(compareKeys, thunk, a, b);
DECLARE_COMPARE_FN(compareTypes, thunk, a, b);
DECLARE_COMPARE_FN(compareLengths, thunk, a, b);
DECLARE_COMPARE_FN(compareValues, thunk, a, b);

// Redis functions

int redisSpyConnect(REDIS* r, char* host, unsigned int port);
int redisSpyServerClearCache(REDIS* redis);
int redisSpyServerRefresh(REDIS* redis);
void redisSpySort(REDIS* redis, int newSortBy);

redisReply* redisSpyGetServerResponse(REDIS* redis, char* command);
int redisSpySendCommandToServer(REDIS* redis, char* command, char* reply, int maxReplyLen);

void redisSpyDump(REDIS* redis, char* delimiter, int unaligned);


int redisSpyGetOptions(int argc, char* argv[], REDIS* redis);

