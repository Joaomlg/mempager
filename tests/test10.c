#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 __attribute__((unused));
	char *page1 __attribute__((unused));
	char *page2 __attribute__((unused));
	char *page3 __attribute__((unused));
	char *page4 __attribute__((unused));
	char *page5 __attribute__((unused));
	char *page6 __attribute__((unused));
	char *page7 __attribute__((unused));
	char *page8 __attribute__((unused));
	char *page9 __attribute__((unused));
	page0 = uvm_extend();
	page1 = uvm_extend();
	page2 = uvm_extend();
	page3 = uvm_extend();
	page4 = uvm_extend();
	page5 = uvm_extend();
	page6 = uvm_extend();
	page7 = uvm_extend();
	page8 = uvm_extend();
	page9 = uvm_extend();
	assert(page9 == NULL);
	exit(EXIT_SUCCESS);
}
