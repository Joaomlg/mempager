#include <sys/types.h>
#include <sys/wait.h>

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "mmu.h"
#include "uvm.h"

int num_forks = 64;
int num_pages = 32;
int num_loops = 32; /* run with ./mmu 256 1024 */
size_t PAGESIZE = 0;
int main(void) {
	PAGESIZE = sysconf(_SC_PAGESIZE);
	int join = 1;
	for(int i = 0; i < num_forks; ++i) {
		pid_t pid = fork();
		if(pid == 0) {
			join = 0;
			break;
		}
	}

	pid_t pid = getpid();
	uvm_create();
	char **pages = malloc(num_pages * sizeof(pages[0]));
	for(int i = 0; i < num_pages; ++i) {
		pages[i] = uvm_extend();
	}

	for(int i = 0; i < num_loops; ++i) {
		for(int j = 0; j < num_pages; ++j) {
			if(pages[j]) {
				sprintf(pages[j]+10, "%d", (int)pid);
				uvm_syslog(pages[j]+10, 10);
			} else {
				int r = uvm_syslog((char *)UVM_BASEADDR + j*PAGESIZE, 6);
				assert(r == -1);
				assert(errno == EINVAL);
			}
		}
	}

	if(!join) exit(EXIT_SUCCESS);
	for(int i = 0; i < num_forks; i++) {
		int status;
		wait(&status);
	}
	exit(EXIT_SUCCESS);
}
