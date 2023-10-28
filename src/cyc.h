/* This module creates rotating filenames based on creating time
 * (cyc_init_periodic) or file size (cyc_init_filesize).  The interface is
 * simple: (1) initialize the cyclic file handle, (2) print stuff to it, and
 * (3) destroy the handle when you are done.
 *
 * These functions use pthread_setcancelstate to prevent file-handling
 * functions to work as cancellation points.  This way the calling thread can
 * be sure that all functions will return.
 *
 * This code is copyrighted by Italo Cunha (cunha@dcc.ufmg.br) and released
 * under the latest version of the GPL. */

#ifndef __CYC_HEADER__
#define __CYC_HEADER__

#include <stdarg.h>

/* This function creates a periodic cyclic file handle.  It names files
 * following the "prefix.%Y%m%d%H%M%S" format string.  New files are created
 * every =period= seconds.  This function makes a copy of =prefix= so the
 * caller may free it. */
struct cyclic * cyc_init_periodic(const char *prefix, unsigned period);

/* This function creates a cyclic file handle based on file size.  It names
 * files following the "prefix.%d" format string, where the number ranges from
 * zero to =nbackups= minus one.  New files are created whenever the (current)
 * "prefix.0" file grows past =maxsize= bytes.  This function makes a copy of
 * =prefix= so the caller may free it. */
struct cyclic * cyc_init_filesize(const char *prefix, unsigned nbackups,
		unsigned maxsize);

/* This function closes the cyclic file handle and frees used memory. */
void cyc_destroy(struct cyclic *cyc);

/* These functions behave similarly to printf and vprintf and return the number
 * of bytes written.  File age and file size, depending on the type of cyclic
 * handle, are checked before printing the message.  This guarantees that that
 * the whole message will be in one file.  These functions flush the output
 * files to disk. */
int cyc_printf(struct cyclic *cyc, const char *fmt, ...);
int cyc_vprintf(struct cyclic *cyc, const char *fmt, va_list ap);

/* This function flushes the current file to disk. */
void cyc_flush(struct cyclic *cyc);

/* This function prevents the current file from changing; they are not
 * protected by any mutexes. */
void cyc_file_lock(struct cyclic *cyc);
void cyc_file_unlock(struct cyclic *cyc);

#endif
