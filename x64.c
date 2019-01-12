
//#include "asm.h"

#include <stdlib.h>
#include <stdint.h>

void* asm_prologue(void* const pgm) {
  /* we have to do this because the stack has to be aligned to a
     16-byte boundary, so the CALL instructions in our body will
     misalign it unless we align it to 8 here */
  *(uint32_t*)pgm = 0x08ec8348U; // subq rsp, 8
  return (char*)pgm + 4;
}

void* asm_epilogue(void* const pgm) {
  /* now undo the alignment stuff we did in the prologue */
  *(uint32_t*)pgm = 0x08c48348U; // addq rsp, 8
  return (char*)pgm + 4;
}

static intptr_t intptrabs(const intptr_t x) {
  if (x < 0) return -x;
  return x;
}

void* asm_call(void* const pgm, const void* const function) {
  uint8_t* pgmc = pgm;

  if (function == NULL) {
    goto longest_jump;
  } else if (intptrabs((intptr_t)function - (intptr_t)pgm) < 0x7fffffe0ULL) {
    *pgmc++ = 0xe8; // callq <32-bit immediate offset>
    *(uint32_t*)pgmc = ((uintptr_t)function - ((uintptr_t)pgmc + 4));
    pgmc += 4;
  } else if ((uintptr_t)function < 0xffffffff) {
    *pgmc++ = 0xb9; // mov ecx, <32-bit immediate>
    *(uint32_t*)pgmc = (uint32_t)function;
    pgmc += 4;
    *pgmc++ = 0xff; // call [rcx]
    *pgmc++ = 0xd1;
  } else {
longest_jump:
    *pgmc++ = 0x48; // movabsq rcx, <64-bit immediate>
    *pgmc++ = 0xb9;
    *(uint64_t*)pgmc = (uint64_t)function;
    pgmc += 8;
    *pgmc++ = 0xff; // callq [rcx]
    *pgmc++ = 0xd1;
  }

  return pgmc;
}

void* asm_ret(void* const pgm) {
  *(uint8_t*)pgm = 0xc3; // retq
  return (uint8_t*)pgm + 1;
}

void asm_patch_call(void* const call, const void* const function) {
  *(uint64_t*)((uint8_t*)call + 2) = (uint64_t)function;
}

void* asm_integer(void* const pgm, const long l) {
  uint8_t* pgmc = pgm;
  
  *(uint32_t*)pgmc = 0x08ef8348U; pgmc += 4; // subq rdi, 8
  *pgmc++ = 0x48; // movabsq rcx, <64-bit immediate>
  *pgmc++ = 0xb9;
  *(uint64_t*)pgmc = l;
  pgmc += 8;
  *(uint32_t*)pgmc = 0x000f8948U; pgmc += 3; // movq [rdi], rcx

  return pgmc;
}

/*

void* asm_pop(void* const pgm) {
  *(uint32_t*)pgm = 0x08c78348U; // add rdi, 8
  return (uint8_t*)pgm + 4;
}

void* asm_dup(void* const pgm) {
  uint8_t* pgmc = pgm;

  *(uint32_t*)pgmc = 0x08ef8348U; pgmc += 4; // subq rdi, 8
  *(uint32_t*)pgmc = 0x084f8b48U; pgmc += 4; // movq rcx, [rdi+8]
  *(uint32_t*)pgmc = 0x000f8948U; pgmc += 3; // movq [rdi], rcx and yes that's a 3

  return pgmc;
}

void* asm_swap(void* const pgm) {
  uint8_t* pgmc = pgm;

  *(uint32_t*)pgmc = 0x084f8b48U; pgmc += 4; // movq rcx, [rdi+8]
  *(uint32_t*)pgmc = 0x00178b48U; pgmc += 3; // movq rdx, [rdi]
  *(uint32_t*)pgmc = 0x000f8948U; pgmc += 3; // movq [rdi], rcx
  *(uint32_t*)pgmc = 0x08578948U; pgmc += 4; // movq [rdi+8], rdx

  return pgmc;
}

void* asm_add(void* const pgm) {
  uint8_t* pgmc = pgm;

  *(uint32_t*)pgmc = 0x084f8b48U; pgmc += 4; // movq rcx, [rdi+8]
  *(uint32_t*)pgmc = 0x00178b48U; pgmc += 3; // movq rdx, [rdi]
  *(uint32_t*)pgmc = 0x08c78348U; pgmc += 4; // add rdi, 8
  *(uint32_t*)pgmc = 0x004801d1U; pgmc += 3; // add rcx, rdx
  *(uint32_t*)pgmc = 0x000f8948U; pgmc += 3; // mov [rdi], rcx

  return pgmc;
}

void* asm_sub(void* pgm) {
  uint8_t* pgmc = pgm;

  *(uint32_t*)pgmc = 0x084f8b48U; pgmc += 4; // movq rcx, [rdi+8]
  *(uint32_t*)pgmc = 0x00178b48U; pgmc += 3; // movq rdx, [rdi]
  *(uint32_t*)pgmc = 0x08c78348U; pgmc += 4; // add rdi, 8
  // *(uint32_t*)pgmc = 0xU; pgmc += 3; // sub rcx, rdx
  *(uint32_t*)pgmc = 0x000f8948U; pgmc += 3; // mov [rdi], rcx

  return pgmc;
}

void* asm_mul(void* pgm) {
  
}

void* asm_div(void* pgm) {
  return ((int(*)())0x12345678U)();
}

*/
