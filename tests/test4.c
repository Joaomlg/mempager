#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	page0[0] = '1';
	printf("this should print 10:\n");
	printf("%c%c\n", page0[0], page0[1]);
	exit(EXIT_SUCCESS);
}
