// Linux SysV Engine-spike scan. Same borrowed graph/mask/output ABI as graph_scan.h.
#if defined(__x86_64__) && defined(__AVX2__) && defined(__ELF__)
.text
.p2align 4
.globl idx_asm_graph_scan64
.type idx_asm_graph_scan64,@function
idx_asm_graph_scan64:
.cfi_startproc
    // Locals0..79, six32B iterator frames80..271, saved GP registers280..327.
    subq $328,%rsp
.cfi_def_cfa_offset 336
    movq %rbx,280(%rsp)
    movq %rbp,288(%rsp)
    movq %r12,296(%rsp)
    movq %r13,304(%rsp)
    movq %r14,312(%rsp)
    movq %r15,320(%rsp)
.cfi_offset %rbx,-56
.cfi_offset %rbp,-48
.cfi_offset %r12,-40
.cfi_offset %r13,-32
.cfi_offset %r14,-24
.cfi_offset %r15,-16
    movq %rdi,%r12                  // Request.
    movq %rsi,0(%rsp)               // Optional counters.
    xorq %r13,%r13                  // Sum modulo 2^64.
    xorq %r14,%r14                  // Selected rows.
    movq $0,16(%rsp)                // Present in-range dirents.
    movq $0,24(%rsp)                // Visited final logical groups.
    movq $0,32(%rsp)                // Entered graph nodes.
    testq %r12,%r12
    jz .Lgx_finish
    cmpq $0,8(%r12)
    je .Lgx_range
    cmpq $0,24(%r12)
    je .Lgx_finish                 // Empty explicit mask never touches root.
.Lgx_range:
    movl 32(%r12),%eax
    movl 36(%r12),%edx
    cmpl %edx,%eax
    ja .Lgx_finish
    movq 32(%r12),%rcx
    movq %rcx,40(%rsp)              // Original inclusive prefix bounds.
    movq 0(%r12),%rdi
    testq %rdi,%rdi
    jz .Lgx_finish
    movq $1,32(%rsp)
    movl %eax,%ebp
    shrl $4,%ebp
    movl %edx,%ebx
    shrl $4,%ebx
    movl 8(%rdi),%esi
    movl 12(%rdi),%edx
    cmpl %edx,%ebp
    ja .Lgx_finish
    cmpl %esi,%ebx
    jb .Lgx_finish
    cmpl %esi,%ebp
    cmovbl %esi,%ebp
    cmpl %edx,%ebx
    cmoval %edx,%ebx
    movq 40(%r12),%rax
    movq %rax,8(%rsp)               // Optional output cursor.
    movzbl 2(%rdi),%r8d
    leal 5(%r8),%ecx
    movl %esi,%eax
    shrl %cl,%eax
    shll %cl,%eax                   // Common high group bits omitted by root.
    movl %r8d,%ecx
    movl %ebp,%edx
    shrl %cl,%edx
    andl $31,%edx
    leaq 80(%rsp),%r15
    movq %rdi,0(%r15)               // {node, group-base, shift, next-slot}.
    movq %rax,8(%r15)
    movl %r8d,16(%r15)
    movl %edx,20(%r15)

.Lgx_seek:
    movq 0(%r15),%rax
    movq 8(%r15),%rdx
    movl 16(%r15),%ecx
    movl 20(%r15),%esi
    cmpl $32,%esi
    jae .Lgx_pop
    movl %esi,%r8d
    shlq %cl,%r8
    orq %rdx,%r8
    cmpq %rbx,%r8
    ja .Lgx_finish
    leal 1(%rsi),%edi
    movl %edi,20(%r15)
    shll $4,%esi
    leaq 64(%rax,%rsi),%r9
    movq 0(%r9),%rdi
    testq %rdi,%rdi
    jz .Lgx_seek
    testl %ecx,%ecx
    jz .Lgx_group
    subl $5,%ecx
    xorl %esi,%esi
    cmpq %rbp,%r8
    jae .Lgx_child
    movl %ebp,%esi
    shrl %cl,%esi
    andl $31,%esi
.Lgx_child:
    addq $32,%r15
    movq %rdi,0(%r15)
    movq %r8,8(%r15)
    movl %ecx,16(%r15)
    movl %esi,20(%r15)
    incq 32(%rsp)
    jmp .Lgx_seek
.Lgx_pop:
    leaq 80(%rsp),%rax
    cmpq %rax,%r15
    je .Lgx_finish
    subq $32,%r15
    jmp .Lgx_seek

.Lgx_group:
    incq 24(%rsp)
    shlq $4,%r8
    movq %r8,56(%rsp)               // Original full u32 prefix coordinates.
    movq %rdi,48(%rsp)              // Header-free directory body.
    movzwl 8(%r9),%r11d
    movq %r11,64(%rsp)              // ORIGINAL mask, retained for rank.
    movl $65535,%r10d
    movl 40(%rsp),%eax
    cmpl %r8d,%eax
    jbe .Lgx_upper
    subl %r8d,%eax
    movl %eax,%ecx
    shll %cl,%r10d
    andl $65535,%r10d
.Lgx_upper:
    leaq 15(%r8),%rdx
    movl 44(%rsp),%eax
    cmpq %rdx,%rax
    jae .Lgx_candidates
    subl %r8d,%eax
    leal 1(%rax),%ecx
    movl $1,%edx
    shll %cl,%edx
    decl %edx
    andl %edx,%r10d
.Lgx_candidates:
    andl %r11d,%r10d
    popcntl %r10d,%eax
    addq %rax,16(%rsp)
    testl %r10d,%r10d
    jz .Lgx_seek
    movq 8(%r12),%rax
    testq %rax,%rax
    jz .Lgx_selected
    movq 16(%r12),%rdx
    cmpq %rdx,%r8
    jb .Lgx_seek
    movq %r8,%r9
    subq %rdx,%r9
    movq 24(%r12),%rdi
    cmpq %rdi,%r9
    jae .Lgx_seek
    movq %r9,%rcx
    shrq $6,%rcx
    movq (%rax,%rcx,8),%rdx
    movl %r9d,%ecx
    andl $63,%ecx
    shrq %cl,%rdx
    andl %edx,%r10d
    subq %r9,%rdi
    cmpq $16,%rdi
    jae .Lgx_selected
    movl %edi,%ecx
    movl $1,%eax
    shll %cl,%eax
    decl %eax
    andl %eax,%r10d                 // Clip the final admitted mask word.
.Lgx_selected:
    testl %r10d,%r10d
    jz .Lgx_seek
.Lgx_row:
    // LLVM-MCA-BEGIN scan_selected_row
    // Actual dependent rank -> locator -> material; no-output loop path only.
    bsfl %r10d,%ecx
    movl $1,%eax
    shll %cl,%eax
    decl %eax
    andl 64(%rsp),%eax
    popcntl %eax,%eax
    shlq $6,%rax
    addq 48(%rsp),%rax
    movq (%rax),%rdx
#if defined(__AVX512F__)
    vmovdqu64 (%rdx),%zmm0
    vextracti64x4 $1,%zmm0,%ymm1
    vpxor %ymm1,%ymm0,%ymm0
#else
    vmovdqu (%rdx),%ymm0
    vpxor 32(%rdx),%ymm0,%ymm0
#endif
    vextracti128 $1,%ymm0,%xmm1
    vpxor %xmm1,%xmm0,%xmm0
    vpsrldq $8,%xmm0,%xmm1
    vpxor %xmm1,%xmm0,%xmm0
    vmovq %xmm0,%rax
    addq %rax,%r13
    incq %r14
    leal -1(%r10),%edx
    andl %edx,%r10d
    cmpq $0,8(%rsp)
    jne .Lgx_output
.Lgx_row_continue:
    testl %r10d,%r10d
    jnz .Lgx_row
    // LLVM-MCA-END scan_selected_row
    jmp .Lgx_seek
.Lgx_output:
    movl 56(%rsp),%edx
    addl %ecx,%edx
    movq 8(%rsp),%rdi
    movl %edx,(%rdi)
    movl $0,4(%rdi)
    movq %rax,8(%rdi)
    addq $16,8(%rsp)
    jmp .Lgx_row_continue

.Lgx_finish:
    movq 0(%rsp),%rax
    testq %rax,%rax
    jz .Lgx_return
    movq 16(%rsp),%rcx
    movq %rcx,0(%rax)
    movq 24(%rsp),%rcx
    movq %rcx,8(%rax)
    movq 32(%rsp),%rcx
    movq %rcx,16(%rax)
    movq %r14,%rcx
    shlq $6,%rcx
    movq %rcx,24(%rax)
.Lgx_return:
    movq %r13,%rax
    movq %r14,%rdx
    movq 280(%rsp),%rbx
    movq 288(%rsp),%rbp
    movq 296(%rsp),%r12
    movq 304(%rsp),%r13
    movq 312(%rsp),%r14
    movq 320(%rsp),%r15
    addq $328,%rsp
.cfi_restore %rbx
.cfi_restore %rbp
.cfi_restore %r12
.cfi_restore %r13
.cfi_restore %r14
.cfi_restore %r15
.cfi_def_cfa_offset 8
    vzeroupper
    retq
.cfi_endproc
.size idx_asm_graph_scan64,.-idx_asm_graph_scan64
#else
#error This Engine scan control requires Linux x86-64 AVX2 or AVX-512.
#endif
.section .note.GNU-stack,""
