all: compile run

compile: pager.c
	gcc -lncursesw pager.c -o pspg -ggdb

run:
	./pspg -d

clean:
	rm ./pspg
