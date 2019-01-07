
asm_prologue:
asm_prologue_end:   

asm_epilogue:
    ret
asm_epilogue_end:

asm_call:
    mov (%rdi), %rsi
    add %rdi, 8
    call *%rsi
asm_call_end:   
    
asm_pop:
    add %rdi, 8
asm_pop_end:

asm_dup:
    
