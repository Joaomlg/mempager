#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include "pager.h"
#include "mmu.h"

typedef struct frame_data {
	pid_t pid;
	int page;
	int prot; /* PROT_READ (clean) or PROT_READ | PROT_WRITE (dirty) */
	int dirty; /* prot may be reset by pager_free_frame() */
} frame_data_t;

typedef struct page_data {
	int block;
	int on_disk; /* 1 indicates page was written to disk */
	int frame; /* -1 indicates non-resident */
} page_data_t;

typedef struct proc {
	pid_t pid;
	int npages;
	int maxpages;
	page_data_t *pages;
} proc_t;

typedef struct pager {
	pthread_mutex_t mutex;
	int nframes;
	int frames_free;
	int clock;
	frame_data_t *frames;
	int nblocks;
	int blocks_free;
	pid_t *block2pid;
	proc_t **pid2proc;
} pager_t;

pager_t pager;

void pager_init(int nframes, int nblocks) {
  pager.nframes = nframes;
  pager.frames_free = nframes;
  pager.frames = (frame_data_t*) malloc(nframes * sizeof(frame_data_t));

  for (int i=0; i<nframes; i++) {
    pager.frames[i].pid = -1;
  }

  pager.nblocks = nblocks;
  pager.blocks_free = nblocks;

  pager.block2pid = (pid_t*) malloc(nblocks * sizeof(pid_t));

  // In the worst case, there will be a process for each block
  pager.pid2proc = (proc_t**) malloc(nblocks * sizeof(pid_t*));
  
  for (int i=0; i<nblocks; i++) {
    pager.block2pid[i] = NULL;
    pager.pid2proc[i] = NULL;
  }
}

void pager_create(pid_t pid) {
  proc_t *proc = (proc_t*) malloc(sizeof(proc_t));
  
  proc->pid = pid;
  proc->npages = 0;
  proc->maxpages = (UVM_MAXADDR - UVM_BASEADDR + 1) / sysconf(_SC_PAGESIZE);
  proc->pages = (page_data_t*) malloc(proc->maxpages * sizeof(page_data_t));

  for (int i=0; i<proc->maxpages; i++) {
    proc->pages[i].frame = -1;
    proc->pages[i].block = -1;
    proc->pages[i].on_disk = 0;
  }

  for (int i=0; i<pager.nblocks; i++) {
    if (pager.pid2proc[i] == NULL) {
      pager.pid2proc[i] = proc;
      break;
    }
  }
}

void *pager_extend(pid_t pid) {
  proc_t *proc = NULL;
  frame_data_t *frame = NULL;

  if (pager.blocks_free == 0) {
    return;
  }

  for (int i=0; i<pager.nblocks; i++) {
    if (pager.pid2proc[i]->pid == pid) {
      proc = pager.pid2proc[i];
      break;
    }
  }

  // TODO: throw an error?!
  if (proc == NULL) {
    return;
  }

  if (proc->npages + 1 > proc->maxpages) {
    return;  // TODO: throw an error?!
  }

  proc->npages++;

  return (void*) UVM_BASEADDR + (proc->npages - 1) * sysconf(_SC_PAGESIZE);
}

void pager_fault(pid_t pid, void *addr) {
  proc_t *proc = NULL;

  for (int i=0; i<pager.nblocks; i++) {
    if (pager.pid2proc[i]->pid == pid) {
      proc = pager.pid2proc[i];
      break;
    }
  }

  if (proc == NULL) {
    return; // TODO: throw an error?!
  }

  int page = ((intptr_t)addr - UVM_BASEADDR) / sysconf(_SC_PAGESIZE);

  if (page >= proc->npages) {
    return;  // TODO: throw an error?! (segmentation fault)
  }

  if (proc->pages[page].frame == -1) {
    // Proc hasn't the page
    // Probably is trying to read
    if (pager.frames_free == 0) {
      return;  // TODO: implement disk swap
    }

    int frame = 0;
    for (; frame<pager.nframes; frame++) {
      if (pager.frames[frame].pid == -1) {
        pager.frames[frame].pid = pid;
        pager.frames[frame].page = page;
        pager.frames[frame].prot = PROT_READ;
        pager.frames_free--;
        break;
      }
    }

    proc->pages[page].frame = frame;
    proc->pages[page].on_disk = 0;
    proc->pages[page].block = -1;

    mmu_zero_fill(frame);
    mmu_resident(pid, addr, frame, pager.frames[frame].prot);
  } else {
    // Proc already has the page
    // Probably is trying to write
    int frame = proc->pages[page].frame;

    pager.frames[frame].prot |= PROT_WRITE;

    mmu_chprot(pid, addr, pager.frames[frame].prot);
  }
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
  
}

void pager_destroy(pid_t pid) {
  
}
