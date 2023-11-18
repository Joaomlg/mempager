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

typedef struct pager {
	int nframes;
	int frames_free;
	frame_data_t *frames;
	int nblocks;
	int blocks_free;
	pid_t *block2pid;
	proc_t **pid2proc;
	int circular_frame_idx;
	pthread_mutex_t mutex;
} pager_t;

typedef struct frame_data {
	pid_t pid;
	int page;
	int prot; /* PROT_READ (clean) or PROT_READ | PROT_WRITE (dirty) */
	int dirty; /* 1 indicates frame was written */
} frame_data_t;

typedef struct proc {
	pid_t pid;
	int npages;
	int maxpages;
	page_data_t *pages;
} proc_t;

typedef struct page_data {
	int frame; /* -1 indicates non-resident */
	int on_disk; /* 1 indicates page was written to disk */
	int block;
} page_data_t;

pager_t *pager;

/****************************************************************************
 * auxiliar functions definitions
 ***************************************************************************/

/* Functions to manage frames */

void pager_clean_frame(frame_data_t *frame);
int pager_get_free_frame();
int pager_release_and_get_frame();
int pager_should_give_frame_second_chance(frame_data_t *frame);
void pager_give_frame_second_chance(frame_data_t *frame);

/* Functions to manage procs */

void pager_clean_proc(proc_t *proc);
proc_t* pager_get_proc(pid_t pid);
int pager_is_proc_page_nonresident(proc_t *proc, int page);
void pager_set_proc_page_write_prot(proc_t *proc, int page);
void pager_reside_proc_page(proc_t *proc, int page);

/* Functions to manage blocks */

void pager_clean_block(int block);
int pager_get_free_block();
page_data_t *pager_get_proc_page_by_frame(proc_t *proc, int frame);

/* Functions to convert virtual address */

int pager_addr_to_page(intptr_t addr);
intptr_t pager_page_to_addr(int page);

/****************************************************************************
 * external functions
 ***************************************************************************/

void pager_init(int nframes, int nblocks) {
  pager = (pager_t*) malloc(sizeof(pager_t));

  if (pager == NULL) {
    handle_error("Cannot allocate memory to pager struct");
  }

  pthread_mutex_init(&pager->mutex, NULL);

  pager->circular_frame_idx = -1;

  pager->nframes = nframes;
  pager->frames_free = nframes;

  pager->frames = (frame_data_t*) malloc(nframes * sizeof(frame_data_t));

  if (pager->frames == NULL) {
    handle_error("Cannot allocate memory to pager frames struct");
  }

  for (int i=0; i<nframes; i++) {
    pager_clean_frame(&pager->frames[i]);
  }

  pager->nblocks = nblocks;
  pager->blocks_free = nblocks;

  pager->block2pid = (pid_t*) malloc(nblocks * sizeof(pid_t));

  if (pager->frames == NULL) {
    handle_error("Cannot allocate memory to pager blocks struct");
  }

  for (int i=0; i<nblocks; i++) {
    pager_clean_block(i);
  }

  // In the worst case, there will be a process for each block
  pager->pid2proc = (proc_t**) malloc(nblocks * sizeof(pid_t*));

  if (pager->pid2proc == NULL) {
    handle_error("Cannot allocate memory to pager proc list struct");
  }
  
  for (int i=0; i<nblocks; i++) {
    pager->pid2proc[i] = (proc_t*) malloc(sizeof(proc_t));

    if (pager->pid2proc[i] == NULL) {
      handle_error("Cannot allocate memory to pager proc struct");
    }

    pager->pid2proc[i]->maxpages = (UVM_MAXADDR - UVM_BASEADDR + 1) / sysconf(_SC_PAGESIZE);
    pager->pid2proc[i]->pages = (page_data_t*) malloc(pager->pid2proc[i]->maxpages * sizeof(page_data_t));

    if (pager->pid2proc[i]->pages == NULL) {
      handle_error("Cannot allocate memory to pager proc page list struct");
    }

    pager_clean_proc(pager->pid2proc[i]);
  }
}

void pager_create(pid_t pid) {
  pthread_mutex_lock(&pager->mutex);

  proc_t *proc = pager_get_proc(-1);

  if (proc == NULL) {
    handle_error("Cannot get a free process");
  }

  proc->pid = pid;

  pthread_mutex_unlock(&pager->mutex);
}

void *pager_extend(pid_t pid) {
  pthread_mutex_lock(&pager->mutex);

  if (pager->blocks_free == 0) {
    pthread_mutex_unlock(&pager->mutex);
    return NULL;
  }

  proc_t *proc = pager_get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  if (proc->npages + 1 > proc->maxpages) {
    pthread_mutex_unlock(&pager->mutex);
    return NULL;
  }

  int block = pager_get_free_block();
  
  pager->block2pid[block] = proc->pid;
  proc->pages[proc->npages].block = block;

  pager->blocks_free--;

  proc->npages++;

  void *vaddr = (void*) pager_page_to_addr(proc->npages - 1);

  pthread_mutex_unlock(&pager->mutex);
  return vaddr;
}

void pager_fault(pid_t pid, void *addr) {
  pthread_mutex_lock(&pager->mutex);

  proc_t *proc = pager_get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  int page = pager_addr_to_page((intptr_t)addr);

  if (page >= proc->npages) {
    handle_error("Process with giving pid cannot access the requested addr");
  }

  if (pager_is_proc_page_nonresident(proc, page)) {
    pager_reside_proc_page(proc, page);
  } else {
    pager_set_proc_page_write_prot(proc, page);
  }

  pthread_mutex_unlock(&pager->mutex);
}

int pager_syslog(pid_t pid, void *addr, size_t len) {
  pthread_mutex_lock(&pager->mutex);
  
  proc_t *proc = pager_get_proc(pid);

  if (proc == NULL) {
    handle_error("Could not find process with giving pid");
  }

  char* buf = (char*) malloc((len + 1) * sizeof(char));

  if (buf == NULL) {
    handle_error("Could not allocate buffer to syslog");
  }

  for (int i=0; i<len; i++) {
    int page = pager_addr_to_page((intptr_t)addr + i);

    if (page >= proc->npages || pager_is_proc_page_nonresident(proc, page)) {
      pthread_mutex_unlock(&pager->mutex);
      return -1;
    }

    buf[i] = (char)pmem[proc->pages[page].frame + i];
  }

  for(int i = 0; i < len; i++) {
    printf("%02x", (unsigned)buf[i]);
    if (i == len - 1) printf("\n");
  }

  free(buf);

  pthread_mutex_unlock(&pager->mutex);
  return 0;
}

void pager_destroy(pid_t pid) {
  pthread_mutex_lock(&pager->mutex);

  proc_t *proc = pager_get_proc(pid);
  pager_clean_proc(proc);

  for (int i=0; i<pager->nframes; i++) {
    if (pager->frames[i].pid == pid) {
      pager_clean_frame(&pager->frames[i]);
      pager->frames_free++;
    }
  }

  for (int i=0; i<pager->nblocks; i++) {
    if (pager->block2pid[i] == pid) {
      pager_clean_block(i);
      pager->blocks_free++;
    }
  }

  pthread_mutex_unlock(&pager->mutex);
}

/****************************************************************************
 * auxiliar functions implementation
 ***************************************************************************/

void pager_clean_frame(frame_data_t *frame) {
  frame->pid = -1;
  frame->page = -1;
  frame->dirty = 0;
  frame->prot = PROT_NONE;
}

int pager_get_free_frame() {
  for (int frame = 0; frame<pager->nframes; frame++) {
    if (pager->frames[frame].pid == -1) {
      return frame;
    }
  }
  return -1;
}

int pager_release_and_get_frame() {
  while(1) {
    pager->circular_frame_idx = (pager->circular_frame_idx + 1) % pager->nframes;

    frame_data_t *frame = &pager->frames[pager->circular_frame_idx];
    
    if (pager_should_give_frame_second_chance(frame)) {
      pager_give_frame_second_chance(frame);
      continue;
    }

    proc_t *proc = pager_get_proc(frame->pid);
    page_data_t *page = pager_get_proc_page_by_frame(proc, pager->circular_frame_idx);

    page->frame = -1;
    mmu_nonresident(proc->pid, (void*)pager_page_to_addr(frame->page));

    if (frame->dirty == 1) {
      mmu_disk_write(pager->circular_frame_idx, page->block);
      page->on_disk = 1;
    }

    pager_clean_frame(frame);
    pager->frames_free++;

    return pager->circular_frame_idx;
  }
}

int pager_should_give_frame_second_chance(frame_data_t *frame) {
  return frame->prot != PROT_NONE;
}

void pager_give_frame_second_chance(frame_data_t *frame) {
  frame->prot = PROT_NONE;
  mmu_chprot(frame->pid, (void*)pager_page_to_addr(frame->page), frame->prot);
}

void pager_clean_proc(proc_t *proc) {
  proc->pid = -1;
  proc->npages = 0;

  for (int j=0; j<proc->maxpages; j++) {
    proc->pages[j].frame = -1;
    proc->pages[j].block = -1;
    proc->pages[j].on_disk = 0;
  }
}

proc_t* pager_get_proc(pid_t pid) {
  for (int i=0; i<pager->nblocks; i++) {
    if (pager->pid2proc[i]->pid == pid) {
      return pager->pid2proc[i];
    }
  }
  return NULL;
}

int pager_is_proc_page_nonresident(proc_t *proc, int page) {
  return proc->pages[page].frame == -1;
}

void pager_set_proc_page_write_prot(proc_t *proc, int page) {
  int frame = proc->pages[page].frame;

  pager->frames[frame].prot |= PROT_WRITE;
  pager->frames[frame].dirty = 1;

  void *vaddr = (void*) pager_page_to_addr(page);

  mmu_chprot(proc->pid, vaddr, pager->frames[frame].prot);
}

void pager_reside_proc_page(proc_t *proc, int page) {
  int frame = pager->frames_free > 0
    ? pager_get_free_frame()
    : pager_release_and_get_frame();

  pager->frames[frame].pid = proc->pid;
  pager->frames[frame].page = page;
  pager->frames[frame].prot = PROT_READ;
  pager->frames_free--;

  if (proc->pages[page].on_disk) {
    mmu_disk_read(proc->pages[page].block, frame);
    proc->pages[page].on_disk = 0;
  } else {
    mmu_zero_fill(frame);
  }

  proc->pages[page].frame = frame;

  void *vaddr = (void*) pager_page_to_addr(page);
  mmu_resident(proc->pid, vaddr, frame, pager->frames[frame].prot);
}

void pager_clean_block(int block) {
  pager->block2pid[block] = -1;
}

int pager_get_free_block() {
  for (int block=0; block<pager->nblocks; block++) {
    if (pager->block2pid[block] == -1) {
      return block;
    }
  }
  return -1;
}

page_data_t *pager_get_proc_page_by_frame(proc_t *proc, int frame) {
  for (int page=0; page<proc->maxpages; page++) {
    if (proc->pages[page].frame == frame) {
      return &proc->pages[page];
    }
  }
  return NULL;
}

int pager_addr_to_page(intptr_t addr) {
  return ((intptr_t)addr - UVM_BASEADDR) / sysconf(_SC_PAGESIZE);
}

intptr_t pager_page_to_addr(int page) {
  return UVM_BASEADDR + page * sysconf(_SC_PAGESIZE);
}
