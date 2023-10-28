#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "cyc.h"

/*****************************************************************************
 * cyclic struct and function declarations
 ****************************************************************************/
#define CYCLIC_LINEBUF 1024
#define CYC_FILESIZE (1<<0)
#define CYC_PERIODIC (1<<1)

struct cyclic {
	int type;
	char *prefix;
	unsigned nbackups;
	unsigned maxsize;
	unsigned period;
	time_t period_start;
	FILE *file;
	pthread_mutex_t lock;
	pthread_mutex_t mutex;
	int flock;
};

static int cyc_check_open_file(struct cyclic *cyc);
static int cyc_open_periodic(struct cyclic *cyc);
static int cyc_open_filesize(struct cyclic *cyc);

/*****************************************************************************
 * cyclic function implementations
 ****************************************************************************/
struct cyclic * cyc_init_periodic(const char *prefix, unsigned period) /* {{{ */
{
	struct cyclic *cyc;
	if(period == 0) return NULL;
	cyc = (struct cyclic *)malloc(sizeof(struct cyclic));
	if(!cyc) goto out;
	cyc->type = CYC_PERIODIC;
	cyc->prefix = strdup(prefix);
	if(!cyc->prefix) goto out;
	cyc->nbackups = -1;
	cyc->maxsize = -1;
	cyc->period = period;
	cyc->period_start = 0;
	cyc->file = NULL;
	if(pthread_mutex_init(&(cyc->lock), NULL)) goto out;
	if(pthread_mutex_init(&(cyc->mutex), NULL)) goto out;
	cyc->flock = 0;
	return cyc;

	out:
	perror("cyc_init_periodic");
	if(cyc && cyc->prefix) free(cyc->prefix);
	if(cyc) free(cyc);
	return NULL;
} /* }}} */

struct cyclic * cyc_init_filesize(const char *prefix, /* {{{ */
		unsigned nbackups, unsigned maxsize)
{
	struct cyclic *cyc;
	if(maxsize == 0) return NULL;
	cyc = (struct cyclic *)malloc(sizeof(struct cyclic));
	if(!cyc) goto out;
	cyc->type = CYC_FILESIZE;
	cyc->prefix = strdup(prefix);
	if(!cyc->prefix) goto out;
	cyc->nbackups = nbackups;
	cyc->maxsize = maxsize;
	cyc->period = -1;
	cyc->period_start = -1;
	cyc->file = NULL;
	if(pthread_mutex_init(&(cyc->lock), NULL)) goto out;
	if(pthread_mutex_init(&(cyc->mutex), NULL)) goto out;
	cyc->flock = 0;
	return cyc;

	out:
	perror("cyc_init_filesize");
	if(cyc && cyc->prefix) free(cyc->prefix);
	if(cyc) free(cyc);
	return NULL;
} /* }}} */

void cyc_destroy(struct cyclic *cyc) /* {{{ */
{
	if(cyc->file) {
		int oldstate;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
		fclose(cyc->file);
		pthread_setcancelstate(oldstate, &oldstate);
	}
	pthread_mutex_destroy(&(cyc->mutex));
	free(cyc->prefix);
	free(cyc);
} /* }}} */

int cyc_printf(struct cyclic *cyc, const char *fmt, ...) /* {{{ */
{
	char line[CYCLIC_LINEBUF];
	va_list ap;
	int oldstate;
	int cnt = 0;
	va_start(ap, fmt);
	pthread_mutex_lock(&cyc->mutex);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if(cyc_check_open_file(cyc)) {
		vsnprintf(line, CYCLIC_LINEBUF, fmt, ap);
		cnt = fputs(line, cyc->file);
		fflush(cyc->file);
	}
	pthread_setcancelstate(oldstate, &oldstate);
	pthread_mutex_unlock(&cyc->mutex);
	va_end(ap);
	return cnt;
} /* }}} */

int cyc_vprintf(struct cyclic *cyc, const char *fmt, va_list ap) /* {{{ */
{
	char line[CYCLIC_LINEBUF];
	int oldstate;
	int cnt = 0;
	pthread_mutex_lock(&cyc->mutex);
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	if(cyc_check_open_file(cyc)) {
		vsnprintf(line, CYCLIC_LINEBUF, fmt, ap);
		cnt = fputs(line, cyc->file);
		fflush(cyc->file);
	}
	pthread_setcancelstate(oldstate, &oldstate);
	pthread_mutex_unlock(&cyc->mutex);
	return cnt;
} /* }}} */

void cyc_flush(struct cyclic *cyc) /* {{{ */
{
	int oldstate;
	pthread_mutex_lock(&cyc->mutex);
	if(!cyc->file) {
		pthread_mutex_unlock(&cyc->mutex);
		return;
	}
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate);
	fflush(cyc->file);
	pthread_setcancelstate(oldstate, &oldstate);
	pthread_mutex_unlock(&cyc->mutex);
} /* }}} */

void cyc_file_lock(struct cyclic *cyc)/*{{{*/
{
	pthread_mutex_lock(&cyc->lock);
	pthread_mutex_lock(&cyc->mutex);
	cyc->flock = 1;
	pthread_mutex_unlock(&cyc->mutex);
}/*}}}*/

void cyc_file_unlock(struct cyclic *cyc)/*{{{*/
{
	pthread_mutex_lock(&cyc->mutex);
	cyc->flock = 0;
	pthread_mutex_unlock(&cyc->mutex);
	pthread_mutex_unlock(&cyc->lock);
}/*}}}*/

/*****************************************************************************
 * static function implementations
 ****************************************************************************/
static int cyc_check_open_file(struct cyclic *cyc) /* {{{ */
{
	if(cyc->flock && cyc->file) return 1;
	switch(cyc->type) {
		case CYC_PERIODIC: {
			unsigned now = time(NULL);
			if(!cyc->file || now-cyc->period_start > cyc->period) {
				return cyc_open_periodic(cyc);
			}
			break;
		}
		case CYC_FILESIZE: {
			if(!cyc->file || ftell(cyc->file) > cyc->maxsize) {
				return cyc_open_filesize(cyc);
			}
			break;
		}
		default: {
			return 0;
			break;
		}
	}
	return 1;
} /* }}} */

static int cyc_open_periodic(struct cyclic *cyc) /* {{{ */
{
	if(cyc->file) fclose(cyc->file);
	cyc->file = NULL;
	cyc->period_start = (time(NULL) / cyc->period) * cyc->period;
	struct tm tm;
	if(!gmtime_r(&cyc->period_start, &tm)) return 0;
	char *fname = malloc(strlen(cyc->prefix) + 80);
	if(!fname) return 0;

	sprintf(fname, "%s.%04d%02d%02d%02d%02d%02d", cyc->prefix,
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

	cyc->file = fopen(fname, "w");
	free(fname);
	if(!cyc->file) return 0;
	setvbuf(cyc->file, NULL, _IOLBF, 0);
	return 1;
} /* }}} */

static int cyc_open_filesize(struct cyclic *cyc) /* {{{ */
{
	if(cyc->file) fclose(cyc->file);
	cyc->file = NULL;
	int bufsz = strlen(cyc->prefix) + 80;
	char *fname = malloc(bufsz);
	if(!fname) return 0;

	int i;
	for(i = cyc->nbackups - 2; i >= 0; i--) {
		fname[0] = '\0';
		sprintf(fname, "%s.%d", cyc->prefix, i);
		if(access(fname, F_OK)) continue;
		char *fnew = malloc(bufsz);
		if(!fnew) goto out_fname;
		fnew[0] = '\0';
		sprintf(fnew, "%s.%d", cyc->prefix, i+1);
		rename(fname, fnew);
		free(fnew);
	}
	fname[0] = '\0';
	sprintf(fname, "%s.0", cyc->prefix);
	cyc->file = fopen(fname, "w");
	free(fname);
	if(!cyc->file) return 0;
	if(setvbuf(cyc->file, NULL, _IOLBF, 0));
	return 1;

	out_fname:
	{ int tmp = errno;
	free(fname);
	errno = tmp; }
	return 0;
} /* }}} */
