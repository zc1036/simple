
#ifndef X64_H
#define X64_H

static inline void call_guest_function(void* guest_function, void*** stackptr) {
  /* Returns new stack pointer. */

  void** new_stackptr;

  long dependency = ((long(*)(void**))guest_function)(*stackptr);

  asm volatile ("mov %%rdi, %0"
                : "=r" (new_stackptr)
                : "r" (dependency));

  *stackptr = new_stackptr;
}

static inline long return_to_guest(void** stackptr) {
  /* The point of this function is just for the compiler to put the new stack
   * pointer into RDI because that's where guest functions expect it to be. It
   * only has a return value so that the compiler will most likely not reorder
   * the call to this function before some other code that might clobber
   * RDI. The actual value of the return is meaningless. */

  long x;

  asm volatile ("mov %1, %%rdi;"
                : "=r" (x)
                : "r" (stackptr)
                : "%rdi");
  return x;
}

#endif
