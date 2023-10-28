#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	page0[5000] = '1';
	printf("this should not run\n");
	exit(EXIT_SUCCESS);
}
