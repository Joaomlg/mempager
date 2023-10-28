/* This module creates a unique log handler for a program.  It supports several
 * helper functions to handle logging of error messages.  This module uses the
 * cyclic file handle module (cyc) to create thread-safe rotating files.  The
 * interface is as follows:
 *
 * (1) initialize the log handler using =log_init=
 * (2) print messages to the file using =logd=, =loge=, and =logea=
 * (3) destroy the logging handler with =log_destroy= when you are done.
 *
 * This code is copyrighted by Italo Cunha (cunha@dcc.ufmg.br) and released
 * under the latest version of the GPL. */

#ifndef __LOG_HEADER__
#define __LOG_HEADER__

#include <inttypes.h>

#define LOG_FATAL 10
#define LOG_ERROR 25
#define LOG_WARN 50
#define LOG_INFO 100
#define LOG_DEBUG 500
#define LOG_EXTRA 1000

/* This function initializes the global logger.  The parameter =verbosity=
 * specifies what gets printed; calls to =logd=, =loge=, and =logea= with lower
 * =verbosity= values will print messages.  The variable =prefix= controls the
 * log files prefix (can include an absolute path).  The =nbackups= and
 * =maxsize= specify the number of rotating log files and their maximum size,
 * respectively. */
void log_init(unsigned verbosity, const char *prefix, unsigned nbackups,
		unsigned maxsize);

void log_destroy(void);
void log_flush(void);

/* This function functions like printf and logs a message if its =verbosity= is
 * lower than that passed to =log_init=. */
void logd(unsigned verbosity, const char *fmt, ...);

/* This functions prints an error message (built with strerror) if =ernno= is
 * set and =verbosity= is lower than that passed to =log_init=.  It should be
 * called with the file name and line number where the error occurred (use the
 * __FILE__ and __LINE__ macros). */
void loge(unsigned verbosity, const char *file, int lineno);

/* Like loge, except it always prints the error message if =errno= is set.
 * This function also prints =msg= and calls =exit= to end the program. */
void logea(const char *file, int lineno, const char *msg)
	__attribute__((noreturn));

/* This function returns a nonzero value if its =verbosity= value is lower than
 * that passed to =log_init=. */
int log_true(unsigned verbosity);

#endif
