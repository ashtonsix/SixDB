#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_group16
.type idx_asm_group16,%function
idx_asm_group16:
.cfi_startproc
    stp x29,x30,[sp,#-48]!
.cfi_def_cfa_offset 48
.cfi_offset 29,-48
.cfi_offset 30,-40
    stp x19,x20,[sp,#16]
    str x21,[sp,#32]
.cfi_offset 19,-32
.cfi_offset 20,-24
.cfi_offset 21,-16
    mov x29,sp
    ldr x19,[x0,#8]
    lsr w3,w1,#4
    add x19,x19,x3
    and w20,w1,#15
    mov x21,x2
    mov x1,x19
    bl idx_module_bind
    cbz x0,.Lg_arm_miss
    ldrh w2,[x0]
    mov w3,#1
    lsl w3,w3,w20
    tst w2,w3
    b.eq .Lg_arm_miss
    sub w3,w3,#1
    and w2,w2,w3
    fmov d0,x2
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w2,v0.b[0]
    add x0,x0,#64
    add x0,x0,x2,lsl #6
    cbz x21,.Lg_arm_done
    stp x0,x1,[x21]
    stp x1,x19,[x21,#16]
    stp w2,w2,[x21,#32]
    stp wzr,wzr,[x21,#40]
    b .Lg_arm_done
.Lg_arm_miss:
    mov x0,xzr
    cbz x21,.Lg_arm_done
    str xzr,[x21]
.Lg_arm_done:
    ldr x21,[sp,#32]
.cfi_restore 21
    ldp x19,x20,[sp,#16]
.cfi_restore 19
.cfi_restore 20
    ldp x29,x30,[sp],#48
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size idx_asm_group16,.-idx_asm_group16
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_group16
.type idx_asm_group16,@function
idx_asm_group16:
.cfi_startproc
    pushq %rbx
.cfi_def_cfa_offset 16
.cfi_offset %rbx,-16
    pushq %r12
.cfi_def_cfa_offset 24
.cfi_offset %r12,-24
    pushq %r13
.cfi_def_cfa_offset 32
.cfi_offset %r13,-32
    movq 8(%rdi),%r12
    movl %esi,%eax
    shrl $4,%eax
    addq %rax,%r12
    movl %esi,%ebx
    andl $15,%ebx
    movq %rdx,%r13
    movq %r12,%rsi
    call idx_module_bind
    testq %rax,%rax
    jz .Lg_x_miss
    movzwl (%rax),%r9d
    movl %ebx,%ecx
    movl $1,%r8d
    shll %cl,%r8d
    testl %r8d,%r9d
    jz .Lg_x_miss
    decl %r8d
    andl %r8d,%r9d
    popcntl %r9d,%r9d
    movl %r9d,%r10d
    shlq $6,%r10
    leaq 64(%rax,%r10),%rax
    testq %r13,%r13
    jz .Lg_x_done
    movq %rax,0(%r13)
    movq %rdx,8(%r13)
    movq %rdx,16(%r13)
    movq %r12,24(%r13)
    movl %r9d,32(%r13)
    movl %r9d,36(%r13)
    movq $0,40(%r13)
    jmp .Lg_x_done
.Lg_x_miss:
    xorl %eax,%eax
    testq %r13,%r13
    jz .Lg_x_done
    movq $0,(%r13)
.Lg_x_done:
    popq %r13
.cfi_restore %r13
.cfi_def_cfa_offset 24
    popq %r12
.cfi_restore %r12
.cfi_def_cfa_offset 16
    popq %rbx
.cfi_restore %rbx
.cfi_def_cfa_offset 8
    retq
.cfi_endproc
.size idx_asm_group16,.-idx_asm_group16
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
