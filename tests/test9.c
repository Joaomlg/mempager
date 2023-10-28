#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

// extend
// write
// read
// swap 
int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	char *page1 = uvm_extend();
	char *page2 = uvm_extend();
	char *page3 = uvm_extend();
	char *page4 = uvm_extend();
	printf("%c\n", page0[0]);
	page1[0] = 'z';
	printf("%c\n", page1[0]);
	printf("%c\n", page2[0]);
	page3[0] = 'z';
	printf("%c\n", page3[0]);
	printf("%c\n", page4[0]);
	printf("%c\n", page0[0]);
	printf("%c\n", page1[0]);
	printf("%c\n", page2[0]);
	printf("%c\n", page3[0]);
	printf("%c\n", page4[0]);
	exit(EXIT_SUCCESS);
}
