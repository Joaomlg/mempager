#include <stdlib.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	char *page0 __attribute__((unused));
	page0 = uvm_extend();
	exit(EXIT_SUCCESS);
}
