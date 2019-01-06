
#ifndef X64_H
#define X64_H

static inline void call_guest_function(void* guest_function, void*** stackptr) {
  /* Returns new stack pointer. */

  void** new_stackptr;

  asm volatile ("mov %1, %%rdi;"
                "call *%2;"
                "mov %%rdi, %0"
                : "=r" (new_stackptr)
                : "r" (*stackptr), "r" (guest_function)
                : "%rdi");

  *stackptr = new_stackptr;
}

static inline long return_to_guest(void** stackptr) {
  /* The point of this function is just for the compiler to put the
   * new stack pointer into RDI because that's where guest functions
   * expect it to be. It only has a return value so that the compiler
   * will most likely not reorder the call to this function before
   * some other code that might clobber RDI. */

  long x;

  asm volatile ("mov %1, %%rdi;"
                "xor %0, %0"
                : "=r" (x)
                : "r" (stackptr)
                : "%rdi");
  return x;
}

#endif
