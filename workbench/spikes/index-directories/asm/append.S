// Closed producer codec:8-byte header +24-byte absolute afterimages, or the
// indexed160-byte-footer alternative. All extents/row domains are admitted by
// the caller. Stores target private disjoint bytes, including failed captures.
#if defined(__aarch64__)
.text
.p2align 2
.globl idx_log_copy8
.type idx_log_copy8,%function
idx_log_copy8:
.cfi_startproc
    cmp x2,#64
    b.lo .Lcopy_tail_arm
.Lcopy64_arm:
    // LLVM-MCA-BEGIN log_copy64
    ldp q0,q1,[x1]
    ldp q2,q3,[x1,#32]
    stp q0,q1,[x0]
    stp q2,q3,[x0,#32]
    add x0,x0,#64
    add x1,x1,#64
    sub x2,x2,#64
    cmp x2,#64
    b.hs .Lcopy64_arm
    // LLVM-MCA-END log_copy64
.Lcopy_tail_arm:
    tbz x2,#5,.Lcopy16_arm
    ldp q0,q1,[x1]
    stp q0,q1,[x0]
    add x0,x0,#32
    add x1,x1,#32
.Lcopy16_arm:
    tbz x2,#4,.Lcopy8_arm
    ldr q0,[x1]
    str q0,[x0]
    add x0,x0,#16
    add x1,x1,#16
.Lcopy8_arm:
    tbz x2,#3,.Lcopy_done_arm
    ldr x3,[x1]
    str x3,[x0]
.Lcopy_done_arm:
    ret
.cfi_endproc
.size idx_log_copy8,.-idx_log_copy8
.globl idx_log_append24
.type idx_log_append24,%function
idx_log_append24:
.cfi_startproc
    // LLVM-MCA-BEGIN log_append24
    mov w1,w1
    stp x1,x2,[x0]
    str x3,[x0,#16]
    ret
    // LLVM-MCA-END log_append24
.cfi_endproc
.size idx_log_append24,.-idx_log_append24
.globl idx_log_find
.type idx_log_find,%function
idx_log_find:
.cfi_startproc
    add x4,x0,x1
    add x0,x0,#8
.Llog_find_1:  cmp x4,x0
    b.eq .Llog_find_3
    sub x4,x4,#24
    ldr w5,[x4]
    cmp w5,w2
    b.ne .Llog_find_1
    ldr x0,[x4,#16]
    ret
.Llog_find_3:  mov x0,x3
    ret
.cfi_endproc
.size idx_log_find,.-idx_log_find
.globl idx_log_index_find
.type idx_log_index_find,%function
idx_log_index_find:
.cfi_startproc
    add x4,x0,x1
    sub x4,x4,#136
    ldrh w4,[x4,w2,uxtw #1]
    cbz w4,.Llog_index_find_1
    add x4,x0,x4,lsl #3
    ldr x0,[x4,#16]
    ret
.Llog_index_find_1:  mov x0,x3
    ret
.cfi_endproc
.size idx_log_index_find,.-idx_log_index_find
.globl idx_log_apply
.type idx_log_apply,%function
idx_log_apply:
.cfi_startproc
    add x3,x0,x1
    add x0,x0,#8
.Llog_apply_1:  cmp x0,x3
    b.eq .Llog_apply_2
    ldr w4,[x0]
    ldr x5,[x0,#16]
    str x5,[x2,w4,uxtw #3]
    add x0,x0,#24
    b .Llog_apply_1
.Llog_apply_2:  ret
.cfi_endproc
.size idx_log_apply,.-idx_log_apply
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_log_copy8
.type idx_log_copy8,@function
idx_log_copy8:
.cfi_startproc
    cmpq $64,%rdx
    jb .Lcopy_tail_x
.Lcopy64_x:
    // LLVM-MCA-BEGIN log_copy64
#if defined(__AVX512F__)
    vmovdqu64 (%rsi),%zmm0
    vmovdqu64 %zmm0,(%rdi)
#else
    vmovdqu (%rsi),%ymm0
    vmovdqu 32(%rsi),%ymm1
    vmovdqu %ymm0,(%rdi)
    vmovdqu %ymm1,32(%rdi)
#endif
    addq $64,%rsi
    addq $64,%rdi
    subq $64,%rdx
    cmpq $64,%rdx
    jae .Lcopy64_x
    // LLVM-MCA-END log_copy64
.Lcopy_tail_x:
    testb $32,%dl
    je .Lcopy16_x
    vmovdqu (%rsi),%ymm0
    vmovdqu %ymm0,(%rdi)
    addq $32,%rsi
    addq $32,%rdi
.Lcopy16_x:
    testb $16,%dl
    je .Lcopy8_x
    vmovdqu (%rsi),%xmm0
    vmovdqu %xmm0,(%rdi)
    addq $16,%rsi
    addq $16,%rdi
.Lcopy8_x:
    testb $8,%dl
    je .Lcopy_done_x
    movq (%rsi),%rax
    movq %rax,(%rdi)
.Lcopy_done_x:
    vzeroupper
    retq
.cfi_endproc
.size idx_log_copy8,.-idx_log_copy8
.globl idx_log_append24
.type idx_log_append24,@function
idx_log_append24:
.cfi_startproc
    // LLVM-MCA-BEGIN log_append24
    movl %esi,%esi
    movq %rsi,(%rdi)
    movq %rdx,8(%rdi)
    movq %rcx,16(%rdi)
    retq
    // LLVM-MCA-END log_append24
.cfi_endproc
.size idx_log_append24,.-idx_log_append24
.globl idx_log_find
.type idx_log_find,@function
idx_log_find:
.cfi_startproc
    leaq (%rdi,%rsi),%r8
    addq $8,%rdi
.Llog_find_1:  cmpq %rdi,%r8
    je .Llog_find_3
    subq $24,%r8
    cmpl %edx,(%r8)
    jne .Llog_find_1
    movq 16(%r8),%rax
    retq
.Llog_find_3:  movq %rcx,%rax
    retq
.cfi_endproc
.size idx_log_find,.-idx_log_find
.globl idx_log_index_find
.type idx_log_index_find,@function
idx_log_index_find:
.cfi_startproc
    leaq -136(%rdi,%rsi),%r8
    movl %edx,%edx
    movzwl (%r8,%rdx,2),%eax
    testl %eax,%eax
    je .Llog_index_find_1
    movq 16(%rdi,%rax,8),%rax
    retq
.Llog_index_find_1:  movq %rcx,%rax
    retq
.cfi_endproc
.size idx_log_index_find,.-idx_log_index_find
.globl idx_log_apply
.type idx_log_apply,@function
idx_log_apply:
.cfi_startproc
    leaq (%rdi,%rsi),%r8
    addq $8,%rdi
.Llog_apply_1:  cmpq %r8,%rdi
    je .Llog_apply_2
    movl (%rdi),%eax
    movq 16(%rdi),%rcx
    movq %rcx,(%rdx,%rax,8)
    addq $24,%rdi
    jmp .Llog_apply_1
.Llog_apply_2:  retq
.cfi_endproc
.size idx_log_apply,.-idx_log_apply
#else
#error unsupported ISA
#endif
.section .note.GNU-stack,""
