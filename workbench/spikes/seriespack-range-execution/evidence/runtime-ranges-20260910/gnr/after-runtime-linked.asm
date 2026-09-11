0000000000192780 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  1928e8:      	callq	0x55b2c0 <benchmark::State::StartKeepRunning()>
  1928ed:      	testl	%ebx, %ebx
  1928ef:      	jne	0x1929a9 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x229>
  1928f5:      	testq	%r15, %r15
  1928f8:      	je	0x1929a9 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x229>
  1928fe:      	xorl	%ebx, %ebx
  192900:      	leaq	0x2f4(%rsp), %r12
  192908:      	nopl	(%rax,%rax)
  192910:      	movq	%r15, 0x88(%rsp)
  192918:      	movl	$0x100, %r15d           # imm = 0x100
  19291e:      	nop
  192920:      	movq	%rbx, %rax
  192923:      	shlq	$0x4, %rax
  192927:      	movq	(%r14,%rax), %r13
  19292b:      	movq	0x8(%r14,%rax), %rbp
  192930:      	leaq	0x1f0(%rsp), %rdi
  192938:      	movq	%r13, %rsi
  19293b:      	movq	%rbp, %rdx
  19293e:      	movq	%r12, %rcx
  192941:      	movl	$0x20, %r8d
  192947:      	callq	*0x260(%rsp)
  19294e:      	movq	%rbx, %rax
  192951:      	incq	%rax
  192954:      	cmpq	$0x1000, %rax           # imm = 0x1000
  19295a:      	movl	$0x0, %ebx
  19295f:      	cmovneq	%rax, %rbx
  192963:      	movl	0x2f4(%rsp), %eax
  19296a:      	notq	%r13
  19296d:      	addq	%r13, %rbp
  192970:      	movl	0x2f4(%rsp,%rbp,4), %ecx
  192977:      	addq	%rax, %rcx
  19297a:      	addq	0x80(%rsp), %rcx
  192982:      	movq	%rcx, 0x80(%rsp)
  19298a:      	decq	%r15
  19298d:      	jne	0x192920 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1a0>
  19298f:      	movq	0x88(%rsp), %r15
  192997:      	testq	%r15, %r15
  19299a:      	jle	0x193220 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xaa0>
  1929a0:      	decq	%r15
  1929a3:      	jne	0x192910 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x190>
  1929a9:      	movq	0x90(%rsp), %rbp
  1929b1:      	movq	%rbp, %rdi
  1929b4:      	callq	0x55b350 <benchmark::State::FinishKeepRunning()>

0000000000194470 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  1945d8:      	callq	0x55b2c0 <benchmark::State::StartKeepRunning()>
  1945dd:      	testl	%ebx, %ebx
  1945df:      	jne	0x194698 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x228>
  1945e5:      	testq	%r15, %r15
  1945e8:      	je	0x194698 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x228>
  1945ee:      	xorl	%ebx, %ebx
  1945f0:      	leaq	0x350(%rsp), %r12
  1945f8:      	nopl	(%rax,%rax)
  194600:      	movq	%r15, 0x88(%rsp)
  194608:      	movl	$0x100, %r15d           # imm = 0x100
  19460e:      	nop
  194610:      	movq	%rbx, %rax
  194613:      	shlq	$0x4, %rax
  194617:      	movq	(%r14,%rax), %r13
  19461b:      	movq	0x8(%r14,%rax), %rbp
  194620:      	leaq	0x1f0(%rsp), %rdi
  194628:      	movq	%r13, %rsi
  19462b:      	movq	%rbp, %rdx
  19462e:      	movq	%r12, %rcx
  194631:      	movl	$0x40, %r8d
  194637:      	callq	*0x260(%rsp)
  19463e:      	movq	%rbx, %rax
  194641:      	incq	%rax
  194644:      	cmpq	$0x1000, %rax           # imm = 0x1000
  19464a:      	movl	$0x0, %ebx
  19464f:      	cmovneq	%rax, %rbx
  194653:      	notq	%r13
  194656:      	addq	%r13, %rbp
  194659:      	movq	0x350(%rsp,%rbp,8), %rax
  194661:      	addq	0x350(%rsp), %rax
  194669:      	addq	0x80(%rsp), %rax
  194671:      	movq	%rax, 0x80(%rsp)
  194679:      	decq	%r15
  19467c:      	jne	0x194610 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x1a0>
  19467e:      	movq	0x88(%rsp), %r15
  194686:      	testq	%r15, %r15
  194689:      	jle	0x194f0f <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xa9f>
  19468f:      	decq	%r15
  194692:      	jne	0x194600 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x190>
  194698:      	movq	0x90(%rsp), %rbp
  1946a0:      	movq	%rbp, %rdi
  1946a3:      	callq	0x55b350 <benchmark::State::FinishKeepRunning()>
