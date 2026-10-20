// Relocatable directory control, calling actual Orbital/Ikea seams.
// Header: count:u16@0, height:u8@2 (zero means leaf). NativeRead ABI is checked
// in native_abi.h. Callee bodies and cache/branch dependencies count separately;
// llvm-mca does not execute the calls or select this control-flow path.
#if defined(__aarch64__)
.text
.p2align 2
.globl idx_asm_exact
.type idx_asm_exact,%function
idx_asm_exact:
.cfi_startproc
    stp x29,x30,[sp,#-96]!
.cfi_def_cfa_offset 96
.cfi_offset 29,-96
.cfi_offset 30,-88
    mov x29,sp
    stp x19,x20,[sp,#16]
    stp x21,x22,[sp,#32]
    stp x23,x24,[sp,#48]
    stp x25,x26,[sp,#64]
    str x27,[sp,#80]
.cfi_offset 19,-80
.cfi_offset 20,-72
.cfi_offset 21,-64
.cfi_offset 22,-56
.cfi_offset 23,-48
.cfi_offset 24,-40
.cfi_offset 25,-32
.cfi_offset 26,-24
.cfi_offset 27,-16
    mov x19,x0
    mov w21,w1
    mov x22,x2
    ldr x20,[x19,#8]
.Larm_node:
    mov x0,x19
    mov x1,x20
    bl idx_module_bind
    cbz x0,.Larm_miss
    mov x23,x0
    mov x24,x1
    ldrh w1,[x23]
    cbz w1,.Larm_miss
    ldrb w3,[x23,#2]
    cbz w3,.Larm_leaf
    ldr w0,[x19,#16]
    add x0,x23,x0
    mov w2,w21
    bl idx_ikea_upper_u32
    cbz w0,.Larm_miss
    sub w0,w0,#1
    ldr w1,[x19,#20]
    add x1,x23,x1
    ldr x20,[x1,x0,lsl #3]
    b .Larm_node
.Larm_leaf:
    ldr w0,[x19,#24]
    add x0,x23,x0
    mov w2,w21
    bl idx_ikea_lower_u32
    ldrh w1,[x23]
    cmp w0,w1
    b.hs .Larm_miss
    mov w25,w0
    ldr w1,[x19,#24]
    add x1,x23,x1
    ldr w1,[x1,w25,uxtw #2]
    cmp w1,w21
    b.ne .Larm_miss
    ldr w1,[x19,#28]
    add x1,x23,x1
    ldrb w26,[x1,x25]
    ldr w1,[x19,#36]
    lsrv w27,w26,w1
    ldr w1,[x19,#40]
    cbz w1,.Larm_independent
    add w27,w27,#1
    mov x0,x24
    mov w1,w27
    bl idx_module_part
    mov x23,x24
    b .Larm_payload
.Larm_independent:
    ldr w1,[x19,#32]
    add x1,x23,x1
    ldr x1,[x1,x27,lsl #3]
    mov x0,x19
    bl idx_module_bind
    mov x23,x1
    mov w27,wzr
.Larm_payload:
    cbz x0,.Larm_miss
    ldr w1,[x19,#36]
    mov w2,#1
    lsl w1,w2,w1
    sub w1,w1,#1
    and w1,w26,w1
    add x0,x0,x1,lsl #6
    cbz x22,.Larm_done
    stp x0,x23,[x22]
    stp x24,x20,[x22,#16]
    stp w25,w26,[x22,#32]
    stp w27,wzr,[x22,#40]
    b .Larm_done
.Larm_miss:
    mov x0,xzr
    cbz x22,.Larm_done
    str xzr,[x22]
.Larm_done:
    ldr x27,[sp,#80]
.cfi_restore 27
    ldp x25,x26,[sp,#64]
.cfi_restore 25
.cfi_restore 26
    ldp x23,x24,[sp,#48]
.cfi_restore 23
.cfi_restore 24
    ldp x21,x22,[sp,#32]
.cfi_restore 21
.cfi_restore 22
    ldp x19,x20,[sp,#16]
.cfi_restore 19
.cfi_restore 20
    ldp x29,x30,[sp],#96
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size idx_asm_exact,.-idx_asm_exact
#elif defined(__x86_64__)
.text
.p2align 4
.globl idx_asm_exact
.type idx_asm_exact,@function
idx_asm_exact:
.cfi_startproc
    pushq %rbp
.cfi_def_cfa_offset 16
.cfi_offset %rbp,-16
    pushq %rbx
.cfi_def_cfa_offset 24
.cfi_offset %rbx,-24
    pushq %r12
.cfi_def_cfa_offset 32
.cfi_offset %r12,-32
    pushq %r13
.cfi_def_cfa_offset 40
.cfi_offset %r13,-40
    pushq %r14
.cfi_def_cfa_offset 48
.cfi_offset %r14,-48
    pushq %r15
.cfi_def_cfa_offset 56
.cfi_offset %r15,-56
    subq $40,%rsp
.cfi_def_cfa_offset 96
    movq %rdi,%r12
    movl %esi,%r13d
    movq %rdx,%r14
    movq 8(%r12),%rbp
.Lx_node:
    movq %r12,%rdi
    movq %rbp,%rsi
    call idx_module_bind
    testq %rax,%rax
    jz .Lx_miss
    movq %rax,%rbx
    movq %rdx,%r15
    movzwl (%rbx),%esi
    testl %esi,%esi
    jz .Lx_miss
    cmpb $0,2(%rbx)
    je .Lx_leaf
    movl 16(%r12),%edi
    addq %rbx,%rdi
    movl %r13d,%edx
    call idx_ikea_upper_u32
    testl %eax,%eax
    jz .Lx_miss
    decl %eax
    movl 20(%r12),%ecx
    addq %rbx,%rcx
    movq (%rcx,%rax,8),%rbp
    jmp .Lx_node
.Lx_leaf:
    movl 24(%r12),%edi
    addq %rbx,%rdi
    movl %r13d,%edx
    call idx_ikea_lower_u32
    movzwl (%rbx),%ecx
    cmpl %ecx,%eax
    jae .Lx_miss
    movl %eax,0(%rsp)
    movl 24(%r12),%ecx
    addq %rbx,%rcx
    cmpl %r13d,(%rcx,%rax,4)
    jne .Lx_miss
    movl 28(%r12),%ecx
    addq %rbx,%rcx
    movzbl (%rcx,%rax),%eax
    movl %eax,4(%rsp)
    movl 36(%r12),%ecx
    shrl %cl,%eax
    cmpl $0,40(%r12)
    je .Lx_independent
    incl %eax
    movl %eax,8(%rsp)
    movq %r15,%rdi
    movl %eax,%esi
    call idx_module_part
    movq %r15,%rdx
    jmp .Lx_payload
.Lx_independent:
    movl 32(%r12),%ecx
    addq %rbx,%rcx
    movq (%rcx,%rax,8),%rsi
    movq %r12,%rdi
    call idx_module_bind
    movl $0,8(%rsp)
.Lx_payload:
    testq %rax,%rax
    jz .Lx_miss
    movq %rdx,16(%rsp)
    movl 36(%r12),%ecx
    movl $1,%edx
    shll %cl,%edx
    decl %edx
    andl 4(%rsp),%edx
    shlq $6,%rdx
    addq %rdx,%rax
    testq %r14,%r14
    jz .Lx_done
    movq %rax,0(%r14)
    movq 16(%rsp),%rdx
    movq %rdx,8(%r14)
    movq %r15,16(%r14)
    movq %rbp,24(%r14)
    movl 0(%rsp),%edx
    movl %edx,32(%r14)
    movl 4(%rsp),%edx
    movl %edx,36(%r14)
    movl 8(%rsp),%edx
    movl %edx,40(%r14)
    movl $0,44(%r14)
    jmp .Lx_done
.Lx_miss:
    xorl %eax,%eax
    testq %r14,%r14
    jz .Lx_done
    movq $0,(%r14)
.Lx_done:
    addq $40,%rsp
.cfi_def_cfa_offset 56
    popq %r15
.cfi_def_cfa_offset 48
    popq %r14
.cfi_def_cfa_offset 40
    popq %r13
.cfi_def_cfa_offset 32
    popq %r12
.cfi_def_cfa_offset 24
    popq %rbx
.cfi_def_cfa_offset 16
    popq %rbp
.cfi_def_cfa_offset 8
    retq
.cfi_endproc
.size idx_asm_exact,.-idx_asm_exact
#else
#error index directory prototype requires AArch64 or x86-64
#endif
.section .note.GNU-stack,""
