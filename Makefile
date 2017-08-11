all: pspg

compile-ncursesw:
	gcc -lncursesw pager.c -o pspg -ggdb

compile-ncurses:
	$(warning "try to use ncurses without wide chars support")
	gcc -lncurses pager.c -o pspg -ggdb

pspg: pager.c
	${MAKE} compile-ncursesw || ${MAKE} compile-ncurses

run:
	./pspg -d

clean:
	rm ./pspg
