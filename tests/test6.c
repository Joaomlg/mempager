#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 = uvm_extend();
	page0[0] = '\0';
	strcat(page0, "hello");
	printf("%s\n", page0);
	uvm_syslog(page0, strlen(page0)+1);
	exit(EXIT_SUCCESS);
}
