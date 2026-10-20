#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_digest64
.type idx_asm_digest64,%function
idx_asm_digest64:
.cfi_startproc
    // LLVM-MCA-BEGIN digest64
    ldp q0,q1,[x0]
    ldp q2,q3,[x0,#32]
    eor v0.16b,v0.16b,v1.16b
    eor v2.16b,v2.16b,v3.16b
    eor v0.16b,v0.16b,v2.16b
    umov x0,v0.d[0]
    umov x1,v0.d[1]
    eor x0,x0,x1
    // LLVM-MCA-END digest64
    ret
.cfi_endproc
.size idx_asm_digest64,.-idx_asm_digest64
.p2align 2
.globl idx_asm_copy64
.type idx_asm_copy64,%function
idx_asm_copy64:
.cfi_startproc
    // LLVM-MCA-BEGIN copy64
    ldp q0,q1,[x1]
    ldp q2,q3,[x1,#32]
    stp q0,q1,[x0]
    stp q2,q3,[x0,#32]
    // LLVM-MCA-END copy64
    ret
.cfi_endproc
.size idx_asm_copy64,.-idx_asm_copy64
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_digest64
.type idx_asm_digest64,@function
idx_asm_digest64:
.cfi_startproc
    // LLVM-MCA-BEGIN digest64
#if defined(__AVX512F__)
    vmovdqu64 (%rdi),%zmm0
    vextracti64x4 $1,%zmm0,%ymm1
    vpxor %ymm1,%ymm0,%ymm0
#elif defined(__AVX2__)
    vmovdqu (%rdi),%ymm0
    vpxor 32(%rdi),%ymm0,%ymm0
#else
#error x86 directory payload control requires AVX2 or AVX-512
#endif
    vextracti128 $1,%ymm0,%xmm1
    vpxor %xmm1,%xmm0,%xmm0
    vmovq %xmm0,%rax
    vpextrq $1,%xmm0,%rdx
    xorq %rdx,%rax
    vzeroupper
    // LLVM-MCA-END digest64
    retq
.cfi_endproc
.size idx_asm_digest64,.-idx_asm_digest64
.p2align 4
.globl idx_asm_copy64
.type idx_asm_copy64,@function
idx_asm_copy64:
.cfi_startproc
    // LLVM-MCA-BEGIN copy64
#if defined(__AVX512F__)
    vmovdqu64 (%rsi),%zmm0
    vmovdqu64 %zmm0,(%rdi)
#else
    vmovdqu (%rsi),%ymm0
    vmovdqu 32(%rsi),%ymm1
    vmovdqu %ymm0,(%rdi)
    vmovdqu %ymm1,32(%rdi)
#endif
    vzeroupper
    // LLVM-MCA-END copy64
    retq
.cfi_endproc
.size idx_asm_copy64,.-idx_asm_copy64
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
