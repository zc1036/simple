
FLAGS := -Wall

.PHONY: clean debug release

debug: ivy
release: ivy

debug: FLAGS += -g

release: FLAGS += -O3

ivy: kernel.c x64.c x64.h Makefile
	gcc $(FLAGS) kernel.c x64.c -o ivy

clean:
	rm -f ivy
