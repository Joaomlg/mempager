#include <stdlib.h>
#include <string.h>

#include "uvm.h"

int main(void) {
	uvm_create();
	uvm_syslog(0, 0);
	exit(EXIT_SUCCESS);
}
