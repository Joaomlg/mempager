/* UNIVERSIDADE FEDERAL DE MINAS GERAIS     *
 * DEPARTAMENTO DE CIENCIA DA COMPUTACAO    *
 * Copyright (c) Italo Fernando Scota Cunha */

#ifndef __MMU_HEADER__
#define __MMU_HEADER__

/* `UVM_BASEADDR` is where virtual pages will be mapped in process
 * virtual address spaces.  This address is not normally used by the
 * Linux kernel.  The page size for the architecture can be obtained
 * using `sysconf(_SC_PAGESIZE)`.  For usual 4KiB pages, a process
 * first page would have addresses ranging from `UVM_BASEADDR` to
 * `UVM_BASEADDR + 0xFFF`. */
#define UVM_BASEADDR ((intptr_t)0x60000000)

/* Programs can allocate a maximum of 1MiB (256 4KiB pages) in the
 * infrastructure; the maximum address managed by the MMU is
 * `UVM_MAXADDR`.  Only faults for addresses between `UVM_BASEADDR`
 * and `UVM_MAXADDR` are sent to the pager. */
#define UVM_MAXADDR ((intptr_t)0x600FFFFF)

/* `pmem` points to the physical memory maintained by the MMU.  Your
 * pager should never write to `pmem`.  */
extern const char *pmem;

/* All functions in this module are blocking, i.e., they only return after
 * changes to physical memory, disk, and program virtual addresses are
 * complete.  */

/* `mmu_zero_fill` will fill `frame` with zeroes (character '0').
 * Your page should use this function to initialize memory before
 * allowing read access to a page.  */
void mmu_zero_fill(int frame);

/* `mmu_resident` will map address `vaddr` in process `pid` to
 * `frame` with protection level `prot`.  `vaddr` should be
 * page-aligned (i.e., `vaddr & (PAGESIZE-1)` should be zero).
 * `prot` should be either `PROT_NONE`, `PROT_READ`, or `PROT_READ
 * | PROT_WRITE`; these constants are defined in <sys/mman.h>.  */
void mmu_resident(pid_t pid, void *vaddr, int frame, int prot);

/* `mmu_nonresident` will mark the page starting at `vaddr` as
 * inacessible by process `pid`.  See `mmu_resident` above for the
 * semantics on `vaddr` and `prot`.  */
void mmu_nonresident(pid_t pid, void *vaddr);

/* `mmu_chprot` will change access permissions for the page starting
 * at `vaddr` to `prot`.  See `mmu_resident` above for the semantics
 * on `vaddr` and `prot`.  */
void mmu_chprot(pid_t pid, void *vaddr, int prot);

/* `mmu_disk_read` copies content from disk block `block_from` into
 * physical frame `frame_to`.  `mmu_disk_write` copies content from
 * frame `frame_from` to disk block `block_to`.  Your pager shoudl
 * use these functions to save paged-out frames.  */
void mmu_disk_read(int block_from, int frame_to);
void mmu_disk_write(int frame_from, int block_to);

#endif
