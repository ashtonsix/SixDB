#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_group16_read
.type idx_asm_group16_read,%function
idx_asm_group16_read:
.cfi_startproc
    stp x19,x30,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 19,-16
.cfi_offset 30,-8
    mov w19,w1
    ldr x1,[x0,#8]
    lsr w2,w19,#4
    add x1,x1,x2
    bl idx_module_bind
    cbz x0,.Lread_arm_miss
    ldrh w2,[x0]
    and w19,w19,#15
    mov w3,#1
    lsl w3,w3,w19
    tst w2,w3
    b.eq .Lread_arm_miss
    sub w3,w3,#1
    and w2,w2,w3
    fmov d0,x2
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w2,v0.b[0]
    add x0,x0,#64
    add x0,x0,x2,lsl #6
    b .Lread_arm_done
.Lread_arm_miss:
    mov x0,xzr
.Lread_arm_done:
    ldp x19,x30,[sp],#16
.cfi_restore 19
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size idx_asm_group16_read,.-idx_asm_group16_read
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_group16_read
.type idx_asm_group16_read,@function
idx_asm_group16_read:
.cfi_startproc
    pushq %rbx
.cfi_def_cfa_offset 16
.cfi_offset %rbx,-16
    movl %esi,%ebx
    shrl $4,%esi
    addq 8(%rdi),%rsi
    call idx_module_bind
    testq %rax,%rax
    jz .Lread_x_miss
    movzwl (%rax),%r9d
    andl $15,%ebx
    movl %ebx,%ecx
    movl $1,%edx
    shll %cl,%edx
    testl %edx,%r9d
    jz .Lread_x_miss
    decl %edx
    andl %edx,%r9d
    popcntl %r9d,%r9d
    shlq $6,%r9
    leaq 64(%rax,%r9),%rax
    jmp .Lread_x_done
.Lread_x_miss:
    xorl %eax,%eax
.Lread_x_done:
    popq %rbx
.cfi_restore %rbx
.cfi_def_cfa_offset 8
    retq
.cfi_endproc
.size idx_asm_group16_read,.-idx_asm_group16_read
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
