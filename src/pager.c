#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#include "pager.h"
#include "mmu.h"

#define handle_error(msg) \
  do { perror(msg); exit(EXIT_FAILURE); } while (0)

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

/****************************************************************************
 * internal functions
 ***************************************************************************/

void clean_frame(frame_data_t *frame) {
  frame->pid = -1;
  frame->page = -1;
  frame->dirty = 0;
  frame->prot = PROT_NONE;
}

void clean_proc(proc_t *proc) {
  proc->pid = -1;
  proc->npages = 0;

  for (int j=0; j<proc->maxpages; j++) {
    proc->pages[j].frame = -1;
    proc->pages[j].block = -1;
    proc->pages[j].on_disk = 0;
  }
}

void clean_block(int block) {
  pager.block2pid[block] = -1;
}

proc_t* get_proc(pid_t pid) {
  for (int i=0; i<pager.nblocks; i++) {
    if (pager.pid2proc[i]->pid == pid) {
      return pager.pid2proc[i];
    }
  }
  return NULL;
}

int get_free_block() {
  for (int block = 0; block<pager.nblocks; block++) {
    if (pager.block2pid[block] == -1) {
      return block;
    }
  }
  return -1;
}

int addr_to_page(intptr_t addr) {
  return ((intptr_t)addr - UVM_BASEADDR) / sysconf(_SC_PAGESIZE);
}

intptr_t page_to_addr(int page) {
  return UVM_BASEADDR + page * sysconf(_SC_PAGESIZE);
}

/****************************************************************************
 * external functions
 ***************************************************************************/

void pager_init(int nframes, int nblocks) {
  pthread_mutex_init(&pager.mutex, NULL);

  pager.clock = -1;

  pager.nframes = nframes;
  pager.frames_free = nframes;

  pager.frames = (frame_data_t*) malloc(nframes * sizeof(frame_data_t));

  for (int i=0; i<nframes; i++) {
    clean_frame(&pager.frames[i]);
  }

  pager.nblocks = nblocks;
  pager.blocks_free = nblocks;

  pager.block2pid = (pid_t*) malloc(nblocks * sizeof(pid_t));

  for (int i=0; i<nblocks; i++) {
    clean_block(i);
  }

  // In the worst case, there will be a process for each block
  pager.pid2proc = (proc_t**) malloc(nblocks * sizeof(pid_t*));
  
  for (int i=0; i<nblocks; i++) {
    pager.pid2proc[i] = (proc_t*) malloc(sizeof(proc_t));
    pager.pid2proc[i]->maxpages = (UVM_MAXADDR - UVM_BASEADDR + 1) / sysconf(_SC_PAGESIZE);
    pager.pid2proc[i]->pages = (page_data_t*) malloc(pager.pid2proc[i]->maxpages * sizeof(page_data_t));

    clean_proc(pager.pid2proc[i]);
  }
}

void pager_create(pid_t pid) {
  pthread_mutex_lock(&pager.mutex);

  proc_t *proc = get_proc(-1);

  if (proc == NULL) {
    handle_error("Cannot get a free process");
  }

  proc->pid = pid;

  pthread_mutex_unlock(&pager.mutex);
}

void *pager_extend(pid_t pid) {
  pthread_mutex_lock(&pager.mutex);

  if (pager.blocks_free == 0) {
    pthread_mutex_unlock(&pager.mutex);
    return NULL;
  }

  proc_t *proc = get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  if (proc->npages + 1 > proc->maxpages) {
    pthread_mutex_unlock(&pager.mutex);
    return NULL;
  }

  int block = get_free_block();

  pager.block2pid[block] = pid;
  pager.blocks_free--;

  proc->pages[proc->npages].block = block;
  proc->npages++;

  void *vaddr = (void*) page_to_addr(proc->npages - 1);

  pthread_mutex_unlock(&pager.mutex);
  return vaddr;
}

void pager_fault(pid_t pid, void *addr) {
  pthread_mutex_lock(&pager.mutex);

  proc_t *proc = get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  int page = addr_to_page((intptr_t)addr);

  if (page >= proc->npages) {
    pthread_mutex_unlock(&pager.mutex);
    return;
  }

  if (proc->pages[page].frame == -1) {
    // Proc hasn't the page
    // Probably is trying to read
    if (pager.frames_free == 0) {
      while(1){
        pager.clock++;
        pager.clock %= pager.nframes;

        void *vaddr = (void*) page_to_addr(pager.frames[pager.clock].page);

        if (pager.frames[pager.clock].prot == PROT_NONE) {
          // Find a frame that can be desalocated

          proc_t *procToDisk = NULL;
          for (int i=0; i<pager.nblocks; i++) {
            if (pager.pid2proc[i]->pid == pager.frames[pager.clock].pid) {
              procToDisk = pager.pid2proc[i];
              break;
            }
          }

          int procToDiskPage = 0;
          for (; procToDiskPage<procToDisk->maxpages; procToDiskPage++) {
            if (procToDisk->pages[procToDiskPage].frame == pager.clock) {
              break;
            }
          }

          procToDisk->pages[procToDiskPage].frame = -1;

          mmu_nonresident(procToDisk->pid, vaddr);

          // Just move to disk if frame is dirty
          if (pager.frames[pager.clock].dirty == 1) {
            mmu_disk_write(pager.clock, procToDisk->pages[procToDiskPage].block);
            procToDisk->pages[procToDiskPage].on_disk = 1;
          }

          clean_frame(&pager.frames[pager.clock]);

          pager.frames_free++;

          break;
        } else {
          // Give a second chance to the frame
          pager.frames[pager.clock].prot = PROT_NONE;
          mmu_chprot(pager.frames[pager.clock].pid, vaddr, pager.frames[pager.clock].prot);
        }
      }
    }

    int frame = 0;
    for (; frame<pager.nframes; frame++) {
      if (pager.frames[frame].pid == -1) {
        break;
      }
    }

    pager.frames[frame].pid = pid;
    pager.frames[frame].page = page;
    pager.frames[frame].prot = PROT_READ;
    pager.frames[frame].dirty = 0;
    pager.frames_free--;

    if (proc->pages[page].on_disk) {
      mmu_disk_read(proc->pages[page].block, frame);
      proc->pages[page].on_disk = 0;
    } else {
      mmu_zero_fill(frame);
    }

    proc->pages[page].frame = frame;

    void *vaddr = (void*) page_to_addr(page);

    mmu_resident(pid, vaddr, frame, pager.frames[frame].prot);
  } else {
    // Proc already has the page
    // Probably is trying to write
    int frame = proc->pages[page].frame;

    pager.frames[frame].prot |= PROT_WRITE;
    pager.frames[frame].dirty = 1;

    void *vaddr = (void*) page_to_addr(page);

    mmu_chprot(pid, vaddr, pager.frames[frame].prot);
  }

  pthread_mutex_unlock(&pager.mutex);
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
  pthread_mutex_lock(&pager.mutex);
  
  proc_t *proc = get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  char* buf = (char*) malloc((len + 1) * sizeof(char));

  if (buf == NULL) {
    handle_error("Could not allocate buffer to syslog");
  }

  for (int i=0; i<len; i++) {
    int page = addr_to_page((intptr_t)addr + i);

    if (page >= proc->npages || proc->pages[page].frame == -1) {
      pthread_mutex_unlock(&pager.mutex);
      return -1;
    }

    buf[i] = (char)pmem[proc->pages[page].frame + i];
  }

  for(int i = 0; i < len; i++) {
    printf("%02x", (unsigned)buf[i]);
    if (i == len - 1) printf("\n");
  }

  free(buf);

  pthread_mutex_unlock(&pager.mutex);
  return 0;
}

void pager_destroy(pid_t pid) {
  pthread_mutex_lock(&pager.mutex);

  proc_t *proc = get_proc(pid);
  clean_proc(proc);

  // Cleaning frames
  for (int i=0; i<pager.nframes; i++) {
    if (pager.frames[i].pid == pid) {
      clean_frame(&pager.frames[i]);
      pager.frames_free++;
    }
  }

  // Cleaning blocks
  for (int i=0; i<pager.nblocks; i++) {
    if (pager.block2pid[i] == pid) {
      clean_block(i);
      pager.blocks_free++;
    }
  }

  pthread_mutex_unlock(&pager.mutex);
}
