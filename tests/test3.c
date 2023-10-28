#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	printf("this should print a zero:\n");
	printf("%c\n", page0[0]);
	exit(EXIT_SUCCESS);
}
