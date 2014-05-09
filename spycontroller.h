#if 0
typedef struct 
{
	int		key;
	int		(*handler)(SPY_WINDOW* w, REDIS* redis);
} REDIS_DISPATCH;
#endif

#define KEY_SEPARATOR	-1

typedef struct 
{
	int			key;
	const char*	desc;
	int	(*handler)(SPY_WINDOW* w, REDIS* redis);
} SPY_DISPATCH;

typedef struct _spy_controller
{
	REDIS*					redis;
	SPY_WINDOW_DELEGATE*	delegate;
} SPY_CONTROLLER;

/*
int redisSpyEventHelp(SPY_WINDOW* w, REDIS* UNUSED(redis));

int redisSpyEventRefresh(SPY_WINDOW* window, REDIS* redis);
void timerExpired(int UNUSED(i));
void redisSpyResetTimer(REDIS* redis, int interval);

int redisSpyGetCommand(SPY_WINDOW* w, REDIS* redis, char* prompt, char* str, int max);
*/

///////////////////////////////////////////////////////////////////////
//
// Event Handlers
//

/*
int redisSpyEventQuit(SPY_WINDOW* window, REDIS* UNUSED(redis));
int redisSpyEventConnectToHost(SPY_WINDOW* window, REDIS* redis);

int redisSpyEventCommand(SPY_WINDOW* window, REDIS* redis);
int redisSpyEventRepeatCommand(SPY_WINDOW* window, REDIS* redis);

int redisSpyEventBatchFile(SPY_WINDOW* window, REDIS* redis);
int redisSpyGetCurrentKeyIndex(SPY_WINDOW* w, REDIS* UNUSED(redis));
int redisSpyEventDeleteKey(SPY_WINDOW* w, REDIS* redis);
int redisSpyEventListPop(SPY_WINDOW* w, REDIS* redis, char* command);
int redisSpyEventListLeftPop(SPY_WINDOW* w, REDIS* redis);
int redisSpyEventListRightPop(SPY_WINDOW* w, REDIS* redis);
int redisSpyEventViewDetails(SPY_WINDOW* w, REDIS* redis);
int redisSpyEventFilterKeys(SPY_WINDOW* window, REDIS* redis);
int redisSpyEventAutoRefresh(SPY_WINDOW* window, REDIS* redis);

int redisSpyEventSortByKey(SPY_WINDOW* window, REDIS* redis);
int redisSpyEventSortByType(SPY_WINDOW* window, REDIS* redis);
int redisSpyEventSortByLength(SPY_WINDOW* window, REDIS* redis);
int redisSpyEventSortByValue(SPY_WINDOW* window, REDIS* redis);

int redisSpyDispatchCommand(int command, SPY_WINDOW* w, REDIS* r);
*/

int spyControllerEventLoop(SPY_WINDOW* w, REDIS* redis);


