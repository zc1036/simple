
ivy: kernel.c x64.c
	gcc -g kernel.c x64.c -o ivy

.PHONY: clean
clean:
	rm -f ivy
