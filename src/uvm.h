/* UNIVERSIDADE FEDERAL DE MINAS GERAIS     *
 * DEPARTAMENTO DE CIENCIA DA COMPUTACAO    *
 * Copyright (c) Italo Fernando Scota Cunha */

#ifndef __UVM_HEADER__
#define __UVM_HEADER__

#include <stdlib.h>

/* `uvm_create` should be called when a program starts to bind it to
 * the memory management infrastructure.  This function sets up
 * a UNIX socket to communicate with the memory management
 * infrastructure and installs a signal handler for SIGSEGV. */
void uvm_create(void);

/* `uvm_extend` allocates a new page for the calling process and
 * returns the address where the page was mapped.  This is analogous
 * to the `sbrk` system call.  Memory allocated with `uvm_extend` is
 * managed by the memory infrastructure, and must not be `free`d.
 * `uvm_extend` fails, returns NULL, and sets `errno` to ENOSPC if
 * the memory infrastructure swap (disk) is out of space.  The
 * system page size is given by `sysconf(_SC_PAGESIZE)`. */
void * uvm_extend(void);

/* `uvm_syslog` requests the memory infrastructure to write the
 * string at `addr` with `len` bytes.  Memory at `addr` must be
 * managed by the memory infrastructure (i.e., allocated with
 * `uvm_extend`).  Returns 0 on success; on failure, returns -1 and
 * sets `errno` to EINVAL. */
int uvm_syslog(void *addr, size_t len);

#endif
