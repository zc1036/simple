
#ifndef ASM_H
#define ASM_H

void* asm_prologue(void* pgm);
void* asm_epilogue(void* pgm);

void* asm_call(void* pgm);

void* asm_pop(void* pgm);
void* asm_dup(void* pgm);
void* asm_swap(void* pgm);

void* asm_add(void* pgm);
void* asm_sub(void* pgm);
void* asm_mul(void* pgm);
void* asm_div(void* pgm);

#endif
