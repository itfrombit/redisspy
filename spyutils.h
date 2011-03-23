#ifndef _SPYUTILS_H_
#define _SPYUTILS_H_

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


#endif

