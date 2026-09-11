0000000000193d90 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  193ef8:      	callq	0x552520 <benchmark::State::StartKeepRunning()>
  193efd:      	testl	%ebx, %ebx
  193eff:      	jne	0x193fe5 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x255>
  193f05:      	testq	%r13, %r13
  193f08:      	je	0x193fe5 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x255>
  193f0e:      	xorl	%r14d, %r14d
  193f11:      	leaq	0x2f4(%rsp), %rbp
  193f19:      	leaq	0x1f0(%rsp), %r15
  193f21:      	nopw	%cs:(%rax,%rax)
  193f30:      	movq	%r13, 0x88(%rsp)
  193f38:      	movl	$0x100, %ebx            # imm = 0x100
  193f3d:      	nopl	(%rax)
  193f40:      	movq	%r14, %rax
  193f43:      	shlq	$0x4, %rax
  193f47:      	movq	(%r12,%rax), %rsi
  193f4b:      	movq	0x8(%r12,%rax), %rdx
  193f50:      	movq	%rdx, %r13
  193f53:      	movq	%rbp, 0x60(%rsp)
  193f58:      	subq	%rsi, %r13
  193f5b:      	movq	%r13, 0x68(%rsp)
  193f60:      	movl	$0x20, 0x70(%rsp)
  193f68:      	movq	0x260(%rsp), %rax
  193f70:      	movq	0x70(%rsp), %rcx
  193f75:      	movq	%rcx, 0x10(%rsp)
  193f7a:      	vmovupd	0x60(%rsp), %xmm0
  193f80:      	vmovupd	%xmm0, (%rsp)
  193f85:      	movq	%r15, %rdi
  193f88:      	callq	*%rax
  193f8a:      	movq	%r14, %rax
  193f8d:      	incq	%rax
  193f90:      	cmpq	$0x1000, %rax           # imm = 0x1000
  193f96:      	movl	$0x0, %r14d
  193f9c:      	cmovneq	%rax, %r14
  193fa0:      	movl	0x2f4(%rsp), %eax
  193fa7:      	movl	0x2f0(%rsp,%r13,4), %ecx
  193faf:      	addq	%rax, %rcx
  193fb2:      	addq	0x80(%rsp), %rcx
  193fba:      	movq	%rcx, 0x80(%rsp)
  193fc2:      	decq	%rbx
  193fc5:      	jne	0x193f40 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1b0>
  193fcb:      	movq	0x88(%rsp), %r13
  193fd3:      	testq	%r13, %r13
  193fd6:      	jle	0x194860 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xad0>
  193fdc:      	decq	%r13
  193fdf:      	jne	0x193f30 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1a0>
  193fe5:      	movq	0x90(%rsp), %rbp
  193fed:      	movq	%rbp, %rdi
  193ff0:      	callq	0x5525b0 <benchmark::State::FinishKeepRunning()>

0000000000195af0 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  195c58:      	callq	0x552520 <benchmark::State::StartKeepRunning()>
  195c5d:      	testl	%ebx, %ebx
  195c5f:      	jne	0x195d43 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x253>
  195c65:      	testq	%r13, %r13
  195c68:      	je	0x195d43 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x253>
  195c6e:      	xorl	%r14d, %r14d
  195c71:      	leaq	0x350(%rsp), %rbp
  195c79:      	leaq	0x1f0(%rsp), %r15
  195c81:      	nopw	%cs:(%rax,%rax)
  195c90:      	movq	%r13, 0x88(%rsp)
  195c98:      	movl	$0x100, %ebx            # imm = 0x100
  195c9d:      	nopl	(%rax)
  195ca0:      	movq	%r14, %rax
  195ca3:      	shlq	$0x4, %rax
  195ca7:      	movq	(%r12,%rax), %rsi
  195cab:      	movq	0x8(%r12,%rax), %rdx
  195cb0:      	movq	%rdx, %r13
  195cb3:      	movq	%rbp, 0x60(%rsp)
  195cb8:      	subq	%rsi, %r13
  195cbb:      	movq	%r13, 0x68(%rsp)
  195cc0:      	movl	$0x40, 0x70(%rsp)
  195cc8:      	movq	0x260(%rsp), %rax
  195cd0:      	movq	0x70(%rsp), %rcx
  195cd5:      	movq	%rcx, 0x10(%rsp)
  195cda:      	vmovupd	0x60(%rsp), %xmm0
  195ce0:      	vmovupd	%xmm0, (%rsp)
  195ce5:      	movq	%r15, %rdi
  195ce8:      	callq	*%rax
  195cea:      	movq	%r14, %rax
  195ced:      	incq	%rax
  195cf0:      	cmpq	$0x1000, %rax           # imm = 0x1000
  195cf6:      	movl	$0x0, %r14d
  195cfc:      	cmovneq	%rax, %r14
  195d00:      	movq	0x348(%rsp,%r13,8), %rax
  195d08:      	addq	0x350(%rsp), %rax
  195d10:      	addq	0x80(%rsp), %rax
  195d18:      	movq	%rax, 0x80(%rsp)
  195d20:      	decq	%rbx
  195d23:      	jne	0x195ca0 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1b0>
  195d29:      	movq	0x88(%rsp), %r13
  195d31:      	testq	%r13, %r13
  195d34:      	jle	0x1965bf <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xacf>
  195d3a:      	decq	%r13
  195d3d:      	jne	0x195c90 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1a0>
  195d43:      	movq	0x90(%rsp), %rbp
  195d4b:      	movq	%rbp, %rdi
  195d4e:      	callq	0x5525b0 <benchmark::State::FinishKeepRunning()>
