
FLAGS := -Wall

.PHONY: clean debug release

debug: simple
release: simple

debug: FLAGS += -g

release: FLAGS += -O3

simple: kernel.c x64.c x64.h Makefile
	gcc $(FLAGS) kernel.c x64.c -o simple

clean:
	rm -f simple
