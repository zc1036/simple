
#ifndef ASM_H
#define ASM_H

void* asm_prologue(void* pgm);
void* asm_epilogue(void* pgm);

void* asm_call(void* pgm, const void* function);
void* asm_ret(void* pgm);

void asm_patch_call(void* call, const void* function);

void* asm_integer(void* pgm, long l);

/*

void* asm_pop(void* pgm);
void* asm_dup(void* pgm);
void* asm_swap(void* pgm);

void* asm_add(void* pgm);
void* asm_sub(void* pgm);
void* asm_mul(void* pgm);
void* asm_div(void* pgm);

*/

#endif
