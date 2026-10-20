#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_clone_patch64
.type idx_asm_clone_patch64,%function
idx_asm_clone_patch64:
.cfi_startproc
    mov x5,x0
.Lcopy_arm:
    // LLVM-MCA-BEGIN clone_block64
    ldp q0,q1,[x1]
    ldp q2,q3,[x1,#32]
    stp q0,q1,[x5]
    stp q2,q3,[x5,#32]
    add x1,x1,#64
    add x5,x5,#64
    subs w2,w2,#1
    b.ne .Lcopy_arm
    // LLVM-MCA-END clone_block64
    str x4,[x0,w3,uxtw]
    ret
.cfi_endproc
.size idx_asm_clone_patch64,.-idx_asm_clone_patch64
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_clone_patch64
.type idx_asm_clone_patch64,@function
idx_asm_clone_patch64:
.cfi_startproc
    movq %rdi,%r9
.Lcopy_x:
    // LLVM-MCA-BEGIN clone_block64
#if defined(__AVX512F__)
    vmovdqu64 (%rsi),%zmm0
    vmovdqu64 %zmm0,(%r9)
#elif defined(__AVX2__)
    vmovdqu (%rsi),%ymm0
    vmovdqu 32(%rsi),%ymm1
    vmovdqu %ymm0,(%r9)
    vmovdqu %ymm1,32(%r9)
#else
#error index directory clone requires AVX2 or AVX-512
#endif
    addq $64,%rsi
    addq $64,%r9
    decl %edx
    jne .Lcopy_x
    // LLVM-MCA-END clone_block64
    movl %ecx,%ecx
    movq %r8,(%rdi,%rcx)
    vzeroupper
    retq
.cfi_endproc
.size idx_asm_clone_patch64,.-idx_asm_clone_patch64
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
