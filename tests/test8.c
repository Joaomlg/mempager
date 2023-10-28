#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

// extend
// swap nowrite
// read 0
// swap nowrite
// read 0
int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	char *page1 = uvm_extend();
	char *page2 = uvm_extend();
	char *page3 = uvm_extend();
	char *page4 = uvm_extend();
	printf("%c\n", page0[0]);
	printf("%c\n", page1[0]);
	printf("%c\n", page2[0]);
	printf("%c\n", page3[0]);
	printf("%c\n", page4[0]);
	printf("%c\n", page0[0]);
	printf("%c\n", page1[0]);
	printf("%c\n", page2[0]);
	printf("%c\n", page3[0]);
	printf("%c\n", page4[0]);
	exit(EXIT_SUCCESS);
}
