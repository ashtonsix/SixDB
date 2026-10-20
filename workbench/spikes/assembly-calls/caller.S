// Four scalar arguments: bytes, minimum, pattern, salt. Return bridge result XOR salt.
// salt is deliberately live across a real compiled call.
#if defined(__aarch64__)
.text
.p2align 2
.globl prototype_handwritten
.type prototype_handwritten, %function
prototype_handwritten:
.cfi_startproc
    stp x29, x30, [sp, #-32]!
.cfi_def_cfa_offset 32
.cfi_offset 29, -32
.cfi_offset 30, -24
    mov x29, sp
    str x19, [sp, #16]
.cfi_offset 19, -16
    mov x19, x3
    bl prototype_bind_demand
    eor x0, x0, x19
    ldr x19, [sp, #16]
.cfi_restore 19
    ldp x29, x30, [sp], #32
.cfi_restore 29
.cfi_restore 30
.cfi_def_cfa_offset 0
    ret
.cfi_endproc
.size prototype_handwritten, .-prototype_handwritten

// Matching arguments/results permit a tail call to the compiled template seam.
.p2align 2
.globl prototype_ikea_lookup
.type prototype_ikea_lookup, %function
prototype_ikea_lookup:
.cfi_startproc
    b prototype_ikea_contains
.cfi_endproc
.size prototype_ikea_lookup, .-prototype_ikea_lookup
#elif defined(__x86_64__)
.text
.p2align 4
.globl prototype_handwritten
.type prototype_handwritten, @function
prototype_handwritten:
.cfi_startproc
    pushq %rbx
.cfi_def_cfa_offset 16
.cfi_offset %rbx, -16
    movq %rcx, %rbx
    call prototype_bind_demand
    xorq %rbx, %rax
    popq %rbx
.cfi_restore %rbx
.cfi_def_cfa_offset 8
    retq
.cfi_endproc
.size prototype_handwritten, .-prototype_handwritten

.p2align 4
.globl prototype_ikea_lookup
.type prototype_ikea_lookup, @function
prototype_ikea_lookup:
.cfi_startproc
    jmp prototype_ikea_contains
.cfi_endproc
.size prototype_ikea_lookup, .-prototype_ikea_lookup
#else
#error assembly-calls supports Linux AArch64 or x86-64
#endif
.section .note.GNU-stack,""
