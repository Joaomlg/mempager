LOGFLAGS=-DUVMLOG -DMMULOG
CFLAGS=-g -Wall $(LOGFLAGS) -I.

all:
	gcc -c $(CFLAGS) log.c
	gcc -c $(CFLAGS) cyc.c
	gcc -c $(CFLAGS) uvm.c
	gcc -c $(CFLAGS) mmu.c
	rm -f uvm.a
	ar -cvq uvm.a uvm.o log.o cyc.o > /dev/null
	rm -f mmu.a
	ar -cvq mmu.a mmu.o log.o cyc.o > /dev/null
	gcc $(CFLAGS) pager.c mmu.a -o mmu -lpthread
	rm -f *.o

clean:
	rm -f *.o *.a mmu tags
