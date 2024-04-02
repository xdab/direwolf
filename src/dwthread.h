#ifndef DWTHREAD_H
#define DWTHREAD_H

#if __WIN32__

#define PTW32_STATIC_LIB

// This enables definitions of localtime_r and gmtime_r in system time.h.
// #define _POSIX_THREAD_SAFE_FUNCTIONS 1
#define _POSIX_C_SOURCE 1

#else
#include <pthread.h>
#endif

#ifdef __APPLE__

// https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2072

// The placement of this is critical.  Putting it earlier was a problem.
// https://github.com/wb2osz/direwolf/issues/113

// It needs to be after the include pthread.h because
// pthread.h pulls in <sys/cdefs.h>, which redefines __DARWIN_C_LEVEL back to ansi,
// which breaks things.

// #define __DARWIN_C_LEVEL  __DARWIN_C_FULL

// There is a more involved patch here:
//  https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2458

#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif

// Defining _DARWIN_C_SOURCE ensures that the definition for the cfmakeraw function (or similar)
// are pulled in through the include file <sys/termios.h>.

#ifdef __DARWIN_C_LEVEL
#undef __DARWIN_C_LEVEL
#endif

#define __DARWIN_C_LEVEL __DARWIN_C_FULL

#endif

#if __WIN32__

typedef CRITICAL_SECTION dw_mutex_t;

#define dw_mutex_init(x) \
	InitializeCriticalSection(x)

/* This one waits for lock. */

#define dw_mutex_lock(x) \
	EnterCriticalSection(x)

/* Returns non-zero if lock was obtained. */

#define dw_mutex_try_lock(x) \
	TryEnterCriticalSection(x)

#define dw_mutex_unlock(x) \
	LeaveCriticalSection(x)

#else

typedef pthread_mutex_t dw_mutex_t;

#define dw_mutex_init(x) pthread_mutex_init(x, NULL)

/* this one will wait. */

#define dw_mutex_lock(x)                                                                            \
	{                                                                                               \
		int err;                                                                                    \
		err = pthread_mutex_lock(x);                                                                \
		if (err != 0)                                                                               \
		{                                                                                           \
			printf("INTERNAL ERROR %s %d pthread_mutex_lock returned %d", __FILE__, __LINE__, err); \
			exit(1);                                                                                \
		}                                                                                           \
	}

/* This one returns true if lock successful, false if not. */
/* pthread_mutex_trylock returns 0 for success. */

#define dw_mutex_try_lock(x)                                                                           \
	({                                                                                                 \
		int err;                                                                                       \
		err = pthread_mutex_trylock(x);                                                                \
		if (err != 0 && err != EBUSY)                                                                  \
		{                                                                                              \
			printf("INTERNAL ERROR %s %d pthread_mutex_trylock returned %d", __FILE__, __LINE__, err); \
			exit(1);                                                                                   \
		};                                                                                             \
		!err;                                                                                          \
	})

#define dw_mutex_unlock(x)                                                                            \
	{                                                                                                 \
		int err;                                                                                      \
		err = pthread_mutex_unlock(x);                                                                \
		if (err != 0)                                                                                 \
		{                                                                                             \
			printf("INTERNAL ERROR %s %d pthread_mutex_unlock returned %d", __FILE__, __LINE__, err); \
			exit(1);                                                                                  \
		}                                                                                             \
	}

#endif

#endif // DWTHREAD_H