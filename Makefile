all: compile run

compile: pager.c
	gcc -lncursesw pager.c -o pager

run:
	./pager
