/* UNIVERSIDADE FEDERAL DE MINAS GERAIS     *
 * DEPARTAMENTO DE CIENCIA DA COMPUTACAO    *
 * Copyright (c) Italo Fernando Scota Cunha */

#include "uvm.h"

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "log.h"

#include "mmu.h"
#include "mmuproto.h"

/****************************************************************************
 * structure definitions and static variables
 ***************************************************************************/
struct uvm_data {/*{{{*/
	int running;
	int npages;
	int sock;
	pthread_t thread;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	char *pmem_fn;
	int pmem_fd;
	intptr_t result;
};/*}}}*/

static struct uvm_data *uvm = NULL;

/****************************************************************************
 * static function declarations
 ***************************************************************************/
static void * uvm_thread(void *data);
static void uvm_exit(int status, void *arg);
static void uvm_segv_action(int signum, siginfo_t *si, void *context);

/* Protocol message handlers assume assume `uvm->mutex` is locked. */
static void uvm_proto_extend_rep(void);
static void uvm_proto_syslog_rep(void);
static void uvm_proto_segv_rep(void);
static void uvm_proto_remap_rep(void);
static void uvm_proto_chprot_rep(void);

/* Helper functions */
static void uvm_connect_socket(int sock, const struct sockaddr_un * addr);

#define NUM_CONNECTION_TRIES 3

#define prexit() do { loge(LOG_FATAL, __FILE__, __LINE__); \
			char buf[80]; sprintf(buf, "%s:%d: ", __FILE__, __LINE__); \
			perror(buf); exit(EXIT_FAILURE); } while(0)

/****************************************************************************
 * external functions
 ***************************************************************************/
void uvm_create(void)/*{{{*/
{
	#ifdef UVMLOG
	log_init(LOG_EXTRA, "uvm.log", 1, 1<<20);
	#endif
	logd(LOG_DEBUG, "uvm_create starting\n");
	assert(uvm == NULL);
	uvm = malloc(sizeof(*uvm));
	if(!uvm) prexit();
	uvm->running = 1;
	uvm->npages = 0;

	logd(LOG_DEBUG, "  connecting unix socket [%s]\n", MMU_PROTO_UNIX_PATH);
	uvm->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(uvm->sock == -1)
		prexit();
	struct sockaddr_un addr;
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	strncat(addr.sun_path, MMU_PROTO_UNIX_PATH, MMU_PROTO_PATH_MAX-1);

	uvm_connect_socket(uvm->sock, &addr);

	logd(LOG_DEBUG, "  sending CREATE_REQ [%d]\n", (int)getpid());
	struct mmu_proto_create_req req;
	req.type = MMU_PROTO_CREATE_REQ;
	req.pid = (uint32_t)getpid();
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req))
		prexit();

	logd(LOG_DEBUG, "  waiting CREATE_REP\n");
	struct mmu_proto_create_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep)) prexit();
	assert(rep.type == MMU_PROTO_CREATE_REP);

	uvm->pmem_fn = strndup(rep.pmem_fn, MMU_PROTO_PATH_MAX);
	logd(LOG_DEBUG, "  mapping pmem_fn [%s]\n", uvm->pmem_fn);
	uvm->pmem_fd = open(uvm->pmem_fn, O_RDWR);
	if(uvm->pmem_fd == -1)
		prexit();

	logd(LOG_DEBUG, "  setting up SEGV handler\n");
	struct sigaction new;
	new.sa_sigaction = uvm_segv_action;
	new.sa_flags = SA_SIGINFO;
	if(sigemptyset(&(new.sa_mask)) == -1)
		prexit();
	sigaction(SIGSEGV, &new, NULL);

	logd(LOG_DEBUG, "  starting uvm_thread()\n");
	pthread_mutex_init(&uvm->mutex, NULL);
	pthread_cond_init(&uvm->cond, NULL);
	pthread_create(&uvm->thread, NULL, uvm_thread, NULL);

	logd(LOG_DEBUG, "  setting up uvm_exit() on_exit()\n");
	if(on_exit(uvm_exit, NULL)) prexit();

	logd(LOG_DEBUG, "uvm_create succeeded\n");
}/*}}}*/

void * uvm_extend(void) {/*{{{*/
	pthread_mutex_lock(&uvm->mutex);
	struct mmu_proto_extend_req req;
	req.type = MMU_PROTO_EXTEND_REQ;
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req))
		prexit();
	pthread_cond_wait(&uvm->cond, &uvm->mutex);
	if(uvm->result) uvm->npages++;
	pthread_mutex_unlock(&uvm->mutex);
	return (void *)uvm->result;
}/*}}}*/

int uvm_syslog(void *addr, size_t len)/*{{{*/
{
	pthread_mutex_lock(&uvm->mutex);
	struct mmu_proto_syslog_req req;
	req.type = MMU_PROTO_SYSLOG_REQ;
	req.addr = (intptr_t)addr;
	req.len = len;
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req))
		prexit();
	pthread_cond_wait(&uvm->cond, &uvm->mutex);
	if(uvm->result != 0) errno = EINVAL;
	pthread_mutex_unlock(&uvm->mutex);
	return (int)uvm->result;
}/*}}}*/

/****************************************************************************
 * auxiliary functions
 ***************************************************************************/
void * uvm_thread(void *data) {/*{{{*/
	logd(LOG_DEBUG, "uvm_thread masking SEGV\n");
	sigset_t sigset;
	if(sigemptyset(&sigset) == -1) prexit();
	if(sigaddset(&sigset, SIGSEGV) == -1) prexit();
	if(sigprocmask(SIG_BLOCK, &sigset, NULL) == -1) prexit();

	while(uvm->running) {
		logd(LOG_DEBUG, "uvm_thread waiting message\n");
		uint32_t type;
		ssize_t c = recv(uvm->sock, &type, sizeof(type), MSG_PEEK);
		if(!uvm->running) break;
		if(c != sizeof(type)) prexit();
		pthread_mutex_lock(&uvm->mutex);
		switch(type) {
			case MMU_PROTO_EXTEND_REP:
				uvm_proto_extend_rep();
				break;
			case MMU_PROTO_SYSLOG_REP:
				uvm_proto_syslog_rep();
				break;
			case MMU_PROTO_SEGV_REP:
				uvm_proto_segv_rep();
				break;
			case MMU_PROTO_REMAP_REP:
				uvm_proto_remap_rep();
				break;
			case MMU_PROTO_CHPROT_REP:
				uvm_proto_chprot_rep();
				break;
			case MMU_PROTO_EXIT_REP:
				uvm->running = 0;
				break;
			default:
				prexit();
				break;
		}
		pthread_mutex_unlock(&uvm->mutex);
	}
	logd(LOG_DEBUG, "uvm_thread exiting\n");
	pthread_exit(NULL);
}/*}}}*/

void uvm_exit(int status, void *arg)/*{{{*/
{
	logd(LOG_DEBUG, "uvm_exit running\n");
	struct mmu_proto_exit_req req;
	req.type = MMU_PROTO_EXIT_REQ;
	/* socket may have been closed by the MMU, ignore return value: */
	send(uvm->sock, &req, sizeof(req), 0);
	pthread_mutex_unlock(&(uvm->mutex));
	pthread_join(uvm->thread, NULL);
	close(uvm->sock);

	pthread_mutex_destroy(&uvm->mutex);
	pthread_cond_destroy(&uvm->cond);
	free(uvm->pmem_fn);
	close(uvm->pmem_fd);
	free(uvm);
	uvm = NULL;
	#ifdef UVMLOG
	log_destroy();
	#endif
}/*}}}*/

void uvm_segv_action(int signum, siginfo_t *si, void *context)/*{{{*/
{
	pthread_mutex_lock(&uvm->mutex);
	assert(si->si_signo == SIGSEGV);
	logd(LOG_DEBUG, "segv addr %p code %d\n", si->si_addr, si->si_code);
	intptr_t va = (intptr_t)si->si_addr;
	if(va < UVM_BASEADDR || va > UVM_MAXADDR) {
		logd(LOG_DEBUG, "external segfault. aborting.\n");
		fprintf(stderr, "(external) segmentation fault\n");
		exit(EXIT_FAILURE);
	}
	size_t pagesz = sysconf(_SC_PAGESIZE);
	if(va >= UVM_BASEADDR + (uvm->npages * pagesz)) {
		logd(LOG_DEBUG, "access to unnallocated MMU address.\n");
		fprintf(stderr, "(internal) segmentation fault.\n");
		fprintf(stderr, "address %p not allocated.\n", (void *)va);
		exit(EXIT_FAILURE);
	}

	struct mmu_proto_segv_req req;
	req.type = MMU_PROTO_SEGV_REQ;
	req.addr = (intptr_t)si->si_addr;
	req.code = si->si_code;
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req)) prexit();

	logd(LOG_DEBUG, "%s waiting service at condition variable\n", __func__);
	pthread_cond_wait(&uvm->cond, &uvm->mutex);
	pthread_mutex_unlock(&uvm->mutex);
	logd(LOG_DEBUG, "%s returning\n", __func__);
}/*}}}*/

/****************************************************************************
 * protocol message handlers
 ***************************************************************************/
void uvm_proto_extend_rep(void)/*{{{*/
{
	logd(LOG_DEBUG, "processing EXTEND_REP\n");
	struct mmu_proto_extend_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		prexit();
	assert(rep.type == MMU_PROTO_EXTEND_REP);
	uvm->result = (intptr_t)rep.vaddr;
	pthread_cond_signal(&uvm->cond);
}/*}}}*/

void uvm_proto_syslog_rep(void)/*{{{*/
{
	logd(LOG_DEBUG, "processing SYSLOG_REP\n");
	struct mmu_proto_syslog_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		prexit();
	assert(rep.type == MMU_PROTO_SYSLOG_REP);
	uvm->result = (intptr_t)rep.retcode;
	pthread_cond_signal(&uvm->cond);
}/*}}}*/

void uvm_proto_segv_rep(void)/*{{{*/
{
	logd(LOG_DEBUG, "processing SEGV_REP\n");
	struct mmu_proto_segv_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		prexit();
	assert(rep.type == MMU_PROTO_SEGV_REP);
	pthread_cond_signal(&uvm->cond);
}/*}}}*/

void uvm_proto_remap_rep(void)/*{{{*/
{
	logd(LOG_DEBUG, "processing REMAP_REP\n");
	struct mmu_proto_remap_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		prexit();
	assert(rep.type == MMU_PROTO_REMAP_REP);
	assert(rep.prot != PROT_NONE);

	assert(rep.vaddr < UINTPTR_MAX);
	void *addr = (void *)(intptr_t)rep.vaddr;
	int prot = (int)rep.prot;
	off_t off = (off_t)rep.offset;
	size_t pagesz = sysconf(_SC_PAGESIZE);
	logd(LOG_DEBUG, "remapping %p at offset %llu prot %d\n", rep.vaddr,
			(unsigned long long)rep.offset, prot);
	munmap(addr, pagesz);
	void *r = mmap(addr, pagesz, prot, MAP_SHARED, uvm->pmem_fd, off);
	if(r != addr)
		prexit();
	logd(LOG_DEBUG, "mprotect %p prot %d\n", rep.vaddr, prot);
	if(mprotect(addr, pagesz, prot) == -1)
		prexit();

	struct mmu_proto_remap_req req;
	req.type = MMU_PROTO_REMAP_REQ;
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req)) prexit();
}/*}}}*/

void uvm_proto_chprot_rep(void)/*{{{*/
{
	logd(LOG_DEBUG, "processing CHPROT_REP\n");
	struct mmu_proto_chprot_rep rep;
	if(recv(uvm->sock, &rep, sizeof(rep), 0) != sizeof(rep))
		prexit();
	assert(rep.type == MMU_PROTO_CHPROT_REP);

	assert(rep.vaddr < UINTPTR_MAX);
	void *addr = (void *)(uintptr_t)rep.vaddr;
	int prot = (int)rep.prot;
	size_t pagesz = sysconf(_SC_PAGESIZE);
	logd(LOG_DEBUG, "mprotect %p prot %d\n", addr, prot);
	if(mprotect(addr, pagesz, prot) == -1)
		prexit();
	/* if(prot == PROT_NONE) {
		logd(LOG_DEBUG, "unmaping %p\n", rep.vaddr);
		if(munmap(addr, pagesz) == -1)
			prexit();
	} */

	struct mmu_proto_chprot_req req;
	req.type = MMU_PROTO_CHPROT_REQ;
	if(send(uvm->sock, &req, sizeof(req), 0) != sizeof(req)) prexit();
}/*}}}*/

/****************************************************************************
 * external functions
 ***************************************************************************/
void uvm_connect_socket(int sock, const struct sockaddr_un * addr) {
	int try = 0;
	do {
		sleep(try);
		if(connect(uvm->sock, (struct sockaddr *)addr, sizeof(*addr)) == 0) {
			return;
		}
		loge(LOG_ERROR, __FILE__, __LINE__);
		logd(LOG_FATAL, "%s connection attempt %d/%d failed\n",
				MMU_PROTO_UNIX_PATH,
				try,
				NUM_CONNECTION_TRIES);
		try += 1;
	} while(try < NUM_CONNECTION_TRIES);
	if(connect(uvm->sock, (struct sockaddr *)addr, sizeof(*addr)) == -1) {
		prexit();
	}
}
