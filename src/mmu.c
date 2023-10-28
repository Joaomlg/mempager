#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

#include "pager.h"
#include "mmuproto.h"

#define MMU_MAX_EVENTS 32
#define MMU_MAX_SOCK 1024


pid_t id2pid[UINT8_MAX];
uint8_t nextid = 0;

/****************************************************************************
 * structure definitions and static variables
 ***************************************************************************/
struct mmu_data {/*{{{*/
	int running;
	int npages;
	char *pmem;
	char *disk;
	char *pmem_fn;
	int pmem_fd;
	int sock;
	struct mmu_client * sock2client[MMU_MAX_SOCK];
};/*}}}*/
struct mmu_client {/*{{{*/
	int running;
	int sock;
	pid_t pid;
	pthread_t thread;
};/*}}}*/
static struct mmu_data *mmu = NULL;
const char *pmem = NULL;
static size_t PAGESIZE = 0;

/****************************************************************************
 * static function declarations
 ***************************************************************************/
static void mmu_destroy(void);
static void mmu_client_destroy(struct mmu_client *c);
static void mmu_shutdown_action(int signum, siginfo_t *si, void *context);
static void mmu_accept_loop(void);
static void * mmu_client_thread(void *vclient);

int get_pid_id(pid_t pid) {
	int i = 0;
	for(; id2pid[i] != pid; i++);
	return i;
}

/****************************************************************************
 * initialization functions {{{
 ***************************************************************************/
static void mmu_init(int npages, int nblocks);
static void mmu_init_disk(int nblocks);
static void mmu_init_pmem(int npages);
static void mmu_init_sock(void);
static void mmu_init_sigs(void);

void mmu_init(int npages, int nblocks)/*{{{*/
{
	PAGESIZE = sysconf(_SC_PAGESIZE);
	assert(mmu == NULL);
	mmu = malloc(sizeof(*mmu));
	if(!mmu) logea(__FILE__, __LINE__, NULL);
	mmu->running = 1;
	mmu->npages = npages;

	mmu_init_disk(nblocks);
	mmu_init_pmem(npages);
	mmu_init_sock();
	mmu_init_sigs();
	memset(mmu->sock2client, 0, MMU_MAX_SOCK*sizeof(mmu->sock2client[0]));
}/*}}}*/

void mmu_init_disk(int nblocks)/*{{{*/
{
	size_t disksz = PAGESIZE * nblocks;
	mmu->disk = malloc(disksz);
	if(!mmu->disk) logea(__FILE__, __LINE__, NULL);
	logd(LOG_INFO, "%s: %zu bytes in %d blocks\n", __func__, disksz, nblocks);
}/*}}}*/

void mmu_init_pmem(int npages)/*{{{*/
{
	mmu->pmem_fn = strdup("mmu.pmem.img.XXXXXX");
	if(mmu->pmem_fn == NULL) logea(__FILE__, __LINE__, NULL);
	mmu->pmem_fd = mkstemp(mmu->pmem_fn);
	if(mmu->pmem_fd == -1) logea(__FILE__, __LINE__, NULL);
	logd(LOG_INFO, "%s: mmap fd %d path %s\n", __func__, mmu->pmem_fd,
			mmu->pmem_fn);

	size_t memsz = PAGESIZE * npages;
	for(size_t i = 0; i < npages*PAGESIZE; ++i) {
		write(mmu->pmem_fd, "z", 1);
	}

	int prot = PROT_READ | PROT_WRITE;
	mmu->pmem = mmap(NULL, memsz, prot, MAP_SHARED, mmu->pmem_fd, 0);
	if(mmu->pmem == MAP_FAILED) logea(__FILE__, __LINE__, NULL);
	pmem = mmu->pmem;
	logd(LOG_INFO, "%s: %zu bytes in %d pages\n", __func__, memsz, npages);
}/*}}}*/

void mmu_init_sock(void)/*{{{*/
{
	mmu->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(mmu->sock == -1)
		logea(__FILE__, __LINE__, NULL);
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	strcat(addr.sun_path, MMU_PROTO_UNIX_PATH);
	if(bind(mmu->sock, (struct sockaddr *)&addr, sizeof(addr)) == -1)
		logea(__FILE__, __LINE__, NULL);
	if(listen(mmu->sock, 32) == -1)
		logea(__FILE__, __LINE__, NULL);
	logd(LOG_INFO, "%s: unix socket %d at %s\n", __func__, mmu->sock,
			MMU_PROTO_UNIX_PATH);
}/*}}}*/

void mmu_init_sigs(void)/*{{{*/
{
	struct sigaction new;
	if(sigemptyset(&(new.sa_mask)) == -1) logea(__FILE__, __LINE__, NULL);
	new.sa_flags = SA_SIGINFO;
	new.sa_sigaction = mmu_shutdown_action;
	sigaction(SIGINT, &new, NULL);
	logd(LOG_INFO, "%s: SIGINT triggers shutdown\n", __func__);
}
/*}}}*/
/*}}}*/

/****************************************************************************
 * shutdown functions {{{
 ***************************************************************************/
void mmu_destroy(void)/*{{{*/
{
	logd(LOG_DEBUG, "%s: starting\n", __func__);
	assert(mmu);
	unlink(mmu->pmem_fn);
	free(mmu->pmem_fn);
	for(int i = 3; i < MMU_MAX_SOCK; ++i) {
		if(!mmu->sock2client[i]) continue;
		mmu_client_destroy(mmu->sock2client[i]);
	}
	munmap(mmu->pmem, mmu->npages * PAGESIZE);
	free(mmu->disk);
	close(mmu->sock);
	unlink(MMU_PROTO_UNIX_PATH);
	free(mmu);
	mmu = NULL;
}
/*}}}*/

void mmu_shutdown_action(int signum, siginfo_t *si, void *context)/*{{{*/
{
	assert(si->si_signo == SIGINT);
	mmu->running = 0;
}
/*}}}*/
/*}}}*/

/****************************************************************************
 * main loop and client functions {{{
 ***************************************************************************/
void mmu_accept_loop(void)/*{{{*/
{
	while(mmu->running) {
		struct sockaddr_un addr;
		socklen_t addrlen = sizeof(addr);
		logd(LOG_DEBUG, "%s: accepting connection\n", __func__);
		int nsock = accept(mmu->sock, (struct sockaddr *)&addr, &addrlen);
		if(nsock == -1) continue;
		logd(LOG_DEBUG, "%s: sock %d\n", __func__, nsock);
		logd(LOG_DEBUG, "%s: creating thread\n", __func__);
		struct mmu_client *c = malloc(sizeof(*c));
		if(!c) logea(__FILE__, __LINE__, NULL);
		mmu->sock2client[nsock] = c;
		c->running = 1;
		c->sock = nsock;
		c->pid = 0;
		pthread_create(&c->thread, NULL, mmu_client_thread, c);
		pthread_detach(c->thread);
	}
	logd(LOG_DEBUG, "%s: exiting\n", __func__);
}/*}}}*/

static void mmu_client_log(const struct mmu_client *c, const char *fname, const char *msg);
static void mmu_client_create(struct mmu_client *c);
static void mmu_client_extend(struct mmu_client *c);
static void mmu_client_syslog(struct mmu_client *c);
static void mmu_client_segv(struct mmu_client *c);
static void mmu_client_exit(struct mmu_client *c);

void * mmu_client_thread(void *vclient)/*{{{*/
{
	struct mmu_client *c = vclient;
	while(mmu->running && c->running) {
		mmu_client_log(c, __func__, "recv");
		uint32_t type;
		ssize_t cnt = recv(c->sock, &type, sizeof(type), MSG_PEEK);
		if(!mmu->running || !c->running) {
			mmu_client_log(c, __func__, "breaking loop");
			break;
		}
		if(cnt != sizeof(type)) goto out_client;
		switch(type) {
		case MMU_PROTO_CREATE_REQ:
			mmu_client_create(c);
			break;
		case MMU_PROTO_EXTEND_REQ:
			mmu_client_extend(c);
			break;
		case MMU_PROTO_SYSLOG_REQ:
			mmu_client_syslog(c);
			break;
		case MMU_PROTO_SEGV_REQ:
			mmu_client_segv(c);
			break;
		case MMU_PROTO_REMAP_REQ:
		case MMU_PROTO_CHPROT_REQ:
			/* these messages are handled by the pager thread */
			break;
		case MMU_PROTO_EXIT_REQ:
			mmu_client_exit(c);
			break;
		default:
			mmu_client_log(c, __func__, "invalid message type");
			goto out_client;
			break;
		}
	}
	mmu_client_log(c, __func__, "finished");
	free(c);
	pthread_exit(NULL);

	out_client:
	mmu_client_destroy(c);
	pthread_exit(NULL);
}/*}}}*/

void mmu_client_log(const struct mmu_client *c, const char *fname, const char *msg)/*{{{*/
{
	logd(LOG_DEBUG, "%s sock %d pid %d: %s\n", fname, c->sock,
			(int)c->pid, msg);
}/*}}}*/

void mmu_client_create(struct mmu_client *c)/*{{{*/
{
	char msg[96];
	struct mmu_proto_create_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_CREATE_REQ);

	c->pid = (pid_t)req.pid;
	int id = nextid;
	id2pid[nextid++] = c->pid;
	printf("pager_create pid %d\n", id);
	pager_create(c->pid);
	snprintf(msg, 96, "create pid %d", id);
	mmu_client_log(c, __func__, msg);

	struct mmu_proto_create_rep rep;
	rep.type = MMU_PROTO_CREATE_REP;
	memset(rep.pmem_fn, '\0', MMU_PROTO_PATH_MAX);
	strncat(rep.pmem_fn, mmu->pmem_fn, MMU_PROTO_PATH_MAX);
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_client_extend(struct mmu_client *c)/*{{{*/
{
	char msg[96];
	struct mmu_proto_extend_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_EXTEND_REQ);

	int id = get_pid_id(c->pid);
	void *vaddr = pager_extend(c->pid);
	printf("pager_extend pid %d vaddr %p\n", id, vaddr);
	snprintf(msg, 96, "extend vaddr %p", vaddr);
	mmu_client_log(c, __func__, msg);

	struct mmu_proto_extend_rep rep;
	rep.type = MMU_PROTO_EXTEND_REP;
	rep.vaddr = (intptr_t)vaddr;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_client_syslog(struct mmu_client *c)/*{{{*/
{
	char msg[96];
	struct mmu_proto_syslog_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_SYSLOG_REQ);

	assert(req.addr < UINTPTR_MAX);
	void *vaddr = (void *)(uintptr_t)req.addr;
	size_t len = (size_t)req.len;
	int id = get_pid_id(c->pid);
	printf("pager_syslog pid %d %p\n", id, vaddr);
	int status = pager_syslog(c->pid, vaddr, len);
	snprintf(msg, 96, "vaddr %p len %zu retcode %d", vaddr, len, status);
	mmu_client_log(c, __func__, msg);

	struct mmu_proto_syslog_rep rep;
	rep.type = MMU_PROTO_SYSLOG_REP;
	rep.retcode = (uint32_t)status;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_client_segv(struct mmu_client *c)/*{{{*/
{
	char msg[96];
	struct mmu_proto_segv_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_SEGV_REQ);

	assert(req.addr < UINTPTR_MAX);
	void *vaddr = (void *)(uintptr_t)req.addr;
	int code = (int)req.code;
	snprintf(msg, 96, "vaddr %p code %d", vaddr, code);
	mmu_client_log(c, __func__, msg);

	int id = get_pid_id(c->pid);
	printf("pager_fault pid %d vaddr %p\n", id, vaddr);
	pager_fault(c->pid, vaddr);

	struct mmu_proto_segv_rep rep;
	rep.type = MMU_PROTO_SEGV_REP;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_client_exit(struct mmu_client *c)/*{{{*/
{
	struct mmu_proto_exit_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	mmu_client_log(c, __func__, "exiting cleanly");
	assert(req.type == MMU_PROTO_EXIT_REQ);
	assert(c->pid);
	int id = get_pid_id(c->pid);
	printf("pager_destroy pid %d\n", id);
	pager_destroy(c->pid);

	struct mmu_proto_segv_rep rep;
	rep.type = MMU_PROTO_EXIT_REP;
	send(c->sock, &rep, sizeof(rep), 0); /* ignoring return value */

	mmu->sock2client[c->sock] = NULL;
	c->running = 0;
	close(c->sock);
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_client_destroy(struct mmu_client *c)/*{{{*/
{
	loge(LOG_WARN, __FILE__, __LINE__);
	mmu_client_log(c, __func__, "running");
	mmu->sock2client[c->sock] = NULL;
	c->running = 0;
	close(c->sock);
	if(c->pid) { /* may get here before CREATE_REQ happens */
		pager_destroy(c->pid);
	}
}/*}}}*/
/*}}}*/

/****************************************************************************
 * external functions {{{
 ***************************************************************************/
struct mmu_client * mmu_client_search(pid_t pid)/*{{{*/
{
	for(int i = 3; i < MMU_MAX_SOCK; ++i) {
		if(!mmu->sock2client[i]) continue;
		if(mmu->sock2client[i]->pid == pid) return mmu->sock2client[i];
	}
	printf("error: pid %d not found.  aborting.\n", (int)pid);
	logd(LOG_FATAL, "pid %d not found.  aborting.\n", (int)pid);
	mmu_destroy();
	exit(EXIT_FAILURE);
}/*}}}*/

void mmu_zero_fill(int frame)/*{{{*/
{
	printf("%s frame %u\n", __func__, frame);
	logd(LOG_DEBUG, "%s frame %u\n", __func__, frame);
	memset(mmu->pmem + (PAGESIZE*frame), '0', PAGESIZE);
}/*}}}*/

void mmu_resident(pid_t pid, void *vaddr, int frame, int prot)/*{{{*/
{
	int id = get_pid_id(pid);
	printf("%s pid %d vaddr %p prot %d frame %u\n", __func__,
			id, vaddr, prot, frame);
	logd(LOG_DEBUG, "%s pid %d vaddr %p prot %d frame %u\n", __func__,
			id, vaddr, prot, frame);
	struct mmu_client *c = mmu_client_search(pid);
	struct mmu_proto_remap_rep rep;
	rep.type = MMU_PROTO_REMAP_REP;
	rep.prot = (int32_t)prot;
	rep.offset = (uint64_t)(PAGESIZE * frame);
	rep.vaddr = (intptr_t)vaddr;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;

	/* We need these functions to wait for the application to
	 * effect the protection change before we return to the
	 * pager.  This loop is necessary because mmu_client_thread
	 * is already in the pager and blocked here (so we cannot
	 * wait on a condition variable to be signaled forward as
	 * there is no one else to recv the REMAP_REQ message).  An
	 * alternative approach would be to use two sockets or
	 * threads. */
	uint32_t t;
	do {
		 if(recv(c->sock, &t, sizeof(t), MSG_PEEK) != sizeof(t))
			goto out_client;
	} while(t != MMU_PROTO_REMAP_REQ);
	struct mmu_proto_remap_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_REMAP_REQ);
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/


void mmu_nonresident(pid_t pid, void *vaddr)/*{{{*/
{
	int id = get_pid_id(pid);
	printf("%s pid %d vaddr %p\n", __func__, id, vaddr);
	logd(LOG_DEBUG, "%s pid %d vaddr %p\n", __func__, id, vaddr);
	struct mmu_client *c = mmu_client_search(pid);
	struct mmu_proto_chprot_rep rep;
	rep.type = MMU_PROTO_CHPROT_REP;
	rep.prot = PROT_NONE;
	rep.vaddr = (intptr_t)vaddr;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;

	uint32_t t;
	do {
		if(recv(c->sock, &t, sizeof(t), MSG_PEEK) != sizeof(t))
			goto out_client;
	} while(t != MMU_PROTO_CHPROT_REQ);
	struct mmu_proto_chprot_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_CHPROT_REQ);
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_chprot(pid_t pid, void *vaddr, int prot)/*{{{*/
{
	int id = get_pid_id(pid);
	printf("%s pid %d vaddr %p prot %d\n", __func__, id, vaddr, prot);
	logd(LOG_DEBUG, "%s pid %d vaddr %p prot %d\n", __func__,
			id, vaddr,prot);
	struct mmu_client *c = mmu_client_search(pid);
	struct mmu_proto_chprot_rep rep;
	rep.type = MMU_PROTO_CHPROT_REP;
	rep.prot = (int32_t)prot;
	rep.vaddr = (intptr_t)vaddr;
	if(send(c->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		goto out_client;

	uint32_t t;
	do {
		if(recv(c->sock, &t, sizeof(t), MSG_PEEK) != sizeof(t))
			goto out_client;
	} while(t != MMU_PROTO_CHPROT_REQ);
	struct mmu_proto_chprot_req req;
	if(recv(c->sock, &req, sizeof(req), 0) != sizeof(req))
		goto out_client;
	assert(req.type == MMU_PROTO_CHPROT_REQ);
	return;

	out_client:
	mmu_client_destroy(c);
}/*}}}*/

void mmu_disk_read(int block_from, int frame_to)/*{{{*/
{
	printf("%s from block %d to frame %d\n", __func__,
			block_from, frame_to);
	logd(LOG_DEBUG, "%s from block %d to frame %d\n", __func__,
			block_from, frame_to);
	memcpy(mmu->pmem + frame_to*PAGESIZE, mmu->disk + block_from*PAGESIZE,
			PAGESIZE);
}/*}}}*/

void mmu_disk_write(int frame_from, int block_to)/*{{{*/
{
	printf("%s from frame %d to block %d\n", __func__,
			frame_from, block_to);
	logd(LOG_DEBUG, "%s from frame %d to block %d\n", __func__,
			frame_from, block_to);
	memcpy(mmu->disk + block_to*PAGESIZE, mmu->pmem + frame_from*PAGESIZE,
			PAGESIZE);
}/*}}}*/
/*}}}*/

/****************************************************************************
 * main() and argparse
 ***************************************************************************/
#ifdef MMUFREE
void pager_free(void);
#endif
void usage(int argc, char **argv) {/*{{{*/
	printf("usage: %s NFRAMES NBLOCKS\n", argv[0]);
	printf("\n");
	printf("valid ranges: 2 <= NFRAMES <= 256\n");
	printf("              4 <= NBLOCKS <= 1024\n");
	exit(EXIT_FAILURE);
}/*}}}*/

int main(int argc, char **argv) {/*{{{*/
	if(argc != 3) usage(argc, argv);
	int npages = atoi(argv[1]);
	if(npages < 1 || npages > 256) usage(argc, argv);
	int nblocks = atoi(argv[2]);
	if(nblocks < 2 || nblocks > 1024) usage(argc, argv);
	#ifdef MMULOG
	log_init(LOG_EXTRA, "mmu.log", 1, 1<<20);
	#endif
	memset(id2pid, 255, UINT8_MAX * sizeof(pid_t));
	mmu_init(npages, nblocks);
	pager_init(npages, nblocks);
	mmu_accept_loop();
	#ifdef MMUFREE
	pager_free();
	#endif
	mmu_destroy();
	#ifdef MMULOG
	log_destroy();
	#endif
}/*}}}*/
