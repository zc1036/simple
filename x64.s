
  .intel_syntax

asm_prologue:
asm_prologue_end:

  nop
  nop

asm_epilogue:
  mov rcx, [rdi+8]
  mov rdx, [rdi]
  add rdi, 8
  imul rcx, rdx
  mov [rdi], rcx

  ret
asm_epilogue_end:
  
asm_call:
asm_call_end:   
    
asm_pop:
  add rdi, 8
asm_pop_end:

asm_dup:

  .global _main 
_main:
  ret
