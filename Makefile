LOGFLAGS=-DUVMLOG -DMMULOG
CFLAGS=-g -Wall -Isrc -std=gnu99

all:
	gcc -c $(CFLAGS) src/log.c
	gcc -c $(CFLAGS) src/cyc.c
	gcc -c $(CFLAGS) $(LOGFLAGS) src/uvm.c
	gcc -c $(CFLAGS) $(LOGFLAGS) src/mmu.c
	rm -f uvm.a
	ar -cvq uvm.a uvm.o log.o cyc.o > /dev/null
	rm -f mmu.a
	ar -cvq mmu.a mmu.o log.o cyc.o > /dev/null
	rm -f *.o
	mkdir -p bin
	gcc $(CFLAGS) tests/test1.c uvm.a -o bin/test1 -lpthread
	gcc $(CFLAGS) tests/test2.c uvm.a -o bin/test2 -lpthread
	gcc $(CFLAGS) tests/test3.c uvm.a -o bin/test3 -lpthread
	gcc $(CFLAGS) tests/test4.c uvm.a -o bin/test4 -lpthread
	gcc $(CFLAGS) tests/test5.c uvm.a -o bin/test5 -lpthread
	gcc $(CFLAGS) tests/test6.c uvm.a -o bin/test6 -lpthread
	gcc $(CFLAGS) tests/test7.c uvm.a -o bin/test7 -lpthread
	gcc $(CFLAGS) tests/test8.c uvm.a -o bin/test8 -lpthread
	gcc $(CFLAGS) tests/test9.c uvm.a -o bin/test9 -lpthread
	gcc $(CFLAGS) tests/test10.c uvm.a -o bin/test10 -lpthread
	gcc $(CFLAGS) tests/test11.c uvm.a -o bin/test11 -lpthread
	gcc $(CFLAGS) tests/test12.c uvm.a -o bin/test12 -lpthread
	gcc $(CFLAGS) src/pager.c mmu.a -o bin/mmu -lpthread
	rm -f uvm.a mmu.a

clean:
	rm -f *.o *.a
	rm -f vgcore.*
	rm -f mmu.sock
	rm -f mmu.pmem.img.*
	rm -f mmu.log.0
	rm -f uvm.log.0
	rm -f test*.out
	rm -rf bin
	pgrep --list-full mmu || true
