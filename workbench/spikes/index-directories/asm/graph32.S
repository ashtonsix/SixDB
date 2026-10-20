// Exact-prefix physical graph control. Actual Scope/Borrowed selects ONE root;
// its interpretation proof owns all immutable child bodies and padding.
#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_graph32
.type idx_asm_graph32,%function
idx_asm_graph32:
.cfi_startproc
    stp x19,x30,[sp,#-16]!
.cfi_def_cfa_offset 16
.cfi_offset 19,-16
.cfi_offset 30,-8
    mov w19,w1
    ldr x1,[x0,#8]
    bl idx_module_bind
    mov w1,w19
    ldp x19,x30,[sp],#16
.cfi_restore 19
.cfi_restore 30
.cfi_def_cfa_offset 0
    b idx_asm_graph32_bound
.cfi_endproc
.size idx_asm_graph32,.-idx_asm_graph32
.p2align 2
.globl idx_asm_graph32_bound
.type idx_asm_graph32_bound,%function
idx_asm_graph32_bound:
.cfi_startproc
    cbz x0,.Lgraph_arm_miss
    mov w5,w1
    lsr w1,w1,#4
    ldp w2,w3,[x0,#8]
    cmp w1,w2
    b.lo .Lgraph_arm_miss
    cmp w1,w3
    b.hi .Lgraph_arm_miss
    // The admitted constructor decrements shift by exactly5 per child.
    ldrb w2,[x0,#2]
.Lgraph_arm_node:
    // LLVM-MCA-BEGIN graph_level
    lsr w3,w1,w2
    and w3,w3,#31
    add x4,x0,#64
    add x4,x4,x3,lsl #4
    ldr x0,[x4]
    cbz x0,.Lgraph_arm_miss
    subs w2,w2,#5
    b.pl .Lgraph_arm_node
    // LLVM-MCA-END graph_level
    ldrh w2,[x4,#8]
    and w5,w5,#15
    mov w3,#1
    lsl w3,w3,w5
    tst w2,w3
    b.eq .Lgraph_arm_miss
    sub w3,w3,#1
    and w2,w2,w3
    fmov d0,x2
    cnt v0.8b,v0.8b
    addv b0,v0.8b
    umov w2,v0.b[0]
    add x0,x0,x2,lsl #6
    b .Lgraph_arm_done
.Lgraph_arm_miss:
    mov x0,xzr
.Lgraph_arm_done:
    ret
.cfi_endproc
.size idx_asm_graph32_bound,.-idx_asm_graph32_bound
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_graph32
.type idx_asm_graph32,@function
idx_asm_graph32:
.cfi_startproc
    pushq %rbx
.cfi_def_cfa_offset 16
.cfi_offset %rbx,-16
    movl %esi,%ebx
    movq 8(%rdi),%rsi
    call idx_module_bind
    movl %ebx,%esi
    movq %rax,%rdi
    popq %rbx
.cfi_restore %rbx
.cfi_def_cfa_offset 8
    jmp idx_asm_graph32_bound
.cfi_endproc
.size idx_asm_graph32,.-idx_asm_graph32
.p2align 4
.globl idx_asm_graph32_bound
.type idx_asm_graph32_bound,@function
idx_asm_graph32_bound:
.cfi_startproc
    movq %rdi,%rax
    movl %esi,%r10d
    testq %rax,%rax
    jz .Lgraph_x_miss
    movl %esi,%r8d
    shrl $4,%r8d
    cmpl 8(%rax),%r8d
    jb .Lgraph_x_miss
    cmpl 12(%rax),%r8d
    ja .Lgraph_x_miss
    movzbl 2(%rax),%ecx
.Lgraph_x_node:
    // LLVM-MCA-BEGIN graph_level
    movl %r8d,%edx
    shrl %cl,%edx
    andl $31,%edx
    shlq $4,%rdx
    leaq 64(%rax,%rdx),%r9
    movq (%r9),%rax
    testq %rax,%rax
    jz .Lgraph_x_miss
    subl $5,%ecx
    jns .Lgraph_x_node
    // LLVM-MCA-END graph_level
    movzwl 8(%r9),%r9d
    andl $15,%r10d
    movl %r10d,%ecx
    movl $1,%edx
    shll %cl,%edx
    testl %edx,%r9d
    jz .Lgraph_x_miss
    decl %edx
    andl %edx,%r9d
    popcntl %r9d,%r9d
    shlq $6,%r9
    leaq (%rax,%r9),%rax
    jmp .Lgraph_x_done
.Lgraph_x_miss:
    xorl %eax,%eax
.Lgraph_x_done:
    retq
.cfi_endproc
.size idx_asm_graph32_bound,.-idx_asm_graph32_bound
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
