.text
# Inputs are the completed exact-match mask in rax, bucket in rdi,
# query key in rsi, and register reduction in r8/r9/r10. These are
# literal selected primary paths, not a CFG or full hash-table estimate.
# LLVM-MCA-BEGIN maintained_hit_tail
  testq %rax,%rax
  je .Labsent
  tzcntq %rax,%rcx
  addq 64(%rdi,%rcx,8),%r10
  incq %r8
  addq %rsi,%r9
# LLVM-MCA-END
# LLVM-MCA-BEGIN maintained_miss_tail
  testq %rax,%rax
  jne .Lhit
  cmpb $0,63(%rdi)
  jne .Lspill
# LLVM-MCA-END
# LLVM-MCA-BEGIN masked_tail
  xorl %edx,%edx
  testq %rax,%rax
  setne %dl
  movzbl 63(%rdi),%ecx
  movl $0,%r11d
  cmovnel %r11d,%ecx
  testl %ecx,%ecx
  jne .Lspill
  movq %rax,%rcx
  orq $65536,%rcx
  tzcntq %rcx,%rcx
  kmovd %edx,%k1
  vmovdqu64 64(%rdi,%rcx,8),%xmm0 {%k1} {z}
  vmovq %xmm0,%rcx
  testq %rax,%rax
  cmoveq %rax,%rcx
  cmoveq %rax,%rsi
  addq %rcx,%r10
  addq %rdx,%r8
  addq %rsi,%r9
# LLVM-MCA-END
# LLVM-MCA-BEGIN carry_pointer_tail
  cmpb $0,63(%rdi)
  jne .Lspill
  leaq 64(%rdi),%rdx
  tzcntq %rax,%rcx
  leaq (%rdx,%rcx,8),%rcx
  cmovcq %r11,%rcx
  setae %dl
  addq (%rcx),%r10
  movzbl %dl,%edx
  addq %rdx,%r8
  negq %rdx
  andq %rdx,%rsi
  addq %rsi,%r9
# LLVM-MCA-END
.Labsent:
.Lhit:
.Lspill:
