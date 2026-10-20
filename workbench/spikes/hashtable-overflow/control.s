# Design sketches for a no-candidate group: rdi=metadata, rsi=mixed hash.
# xmm3 is a preloaded 0x80-byte vector in baseline. Outputs: eax=candidates,
# flags (or dl)=stop condition. There is no repeated pointer/result dependency.
# These sketches omit branch prediction, slot loads and cache effects. The
# pinned znver5 selector uses a Zen4 scheduling proxy. Native codegen/timing wins.
.text
.globl overflow_control_sketch
overflow_control_sketch:
# LLVM-MCA-BEGIN baseline7
    movq %rsi, %rax
    shrq $57, %rax
    vmovd %eax, %xmm0
    vpbroadcastb %xmm0, %xmm0
    vmovdqa (%rdi), %xmm1
    vpcmpeqb %xmm1, %xmm0, %xmm0
    vpmovmskb %xmm0, %eax
    vpcmpeqb %xmm1, %xmm3, %xmm2
    vpmovmskb %xmm2, %ecx
    testl %ecx, %ecx
# LLVM-MCA-END
# LLVM-MCA-BEGIN tag8
    movq %rsi, %rax
    shrq $56, %rax
    leal 8(%rax), %ecx
    cmpl $2, %eax
    cmovbl %ecx, %eax
    vmovd %eax, %xmm0
    vpbroadcastb %xmm0, %xmm0
    vmovdqa (%rdi), %xmm1
    vpcmpeqb %xmm1, %xmm0, %xmm0
    vpmovmskb %xmm0, %eax
    vpxor %xmm3, %xmm3, %xmm3
    vpcmpeqb %xmm1, %xmm3, %xmm2
    vpmovmskb %xmm2, %ecx
    testl %ecx, %ecx
# LLVM-MCA-END
# LLVM-MCA-BEGIN filter15
    movq %rsi, %rax
    shrq $56, %rax
    movl %eax, %ecx
    andl $7, %ecx
    leal 8(%rax), %edx
    cmpl $2, %eax
    cmovbl %edx, %eax
    vmovd %eax, %xmm0
    vpbroadcastb %xmm0, %xmm0
    vmovdqa (%rdi), %xmm1
    vpcmpeqb %xmm1, %xmm0, %xmm0
    vpmovmskb %xmm0, %eax
    andl $32767, %eax
    movzbl 15(%rdi), %edx
    btl %ecx, %edx
    setae %dl
# LLVM-MCA-END
    ret
.section .note.GNU-stack,"",@progbits
