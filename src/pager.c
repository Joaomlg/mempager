#include <pthread.h>

#include "pager.h"
#include "mmu.h"

struct frame_data {
	pid_t pid;
	int page;
	int prot; /* PROT_READ (clean) or PROT_READ | PROT_WRITE (dirty) */
	int dirty; /* prot may be reset by pager_free_frame() */
};

struct page_data {
	int block;
	int on_disk; /* 0 indicates page was written to disk */
	int frame; /* -1 indicates non-resident */
};

struct proc {
	pid_t pid;
	int npages;
	int maxpages;
	struct page_data *pages;
};

struct pager {
	pthread_mutex_t mutex;
	int nframes;
	int frames_free;
	int clock;
	struct frame_data *frames;
	int nblocks;
	int blocks_free;
	pid_t *block2pid;
	struct proc **pid2proc;
};


void pager_init(int nframes, int nblocks) {
  
}

void pager_create(pid_t pid) {
  
}

void *pager_extend(pid_t pid) {
  
}

void pager_fault(pid_t pid, void *addr) {
  
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
  
}

void pager_destroy(pid_t pid) {
  
}
