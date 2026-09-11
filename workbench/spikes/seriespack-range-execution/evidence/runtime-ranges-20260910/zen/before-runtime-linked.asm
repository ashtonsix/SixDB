0000000000202550 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  202691:      	callq	0x64aab0 <benchmark::State::StartKeepRunning()>
  202696:      	testl	%ebx, %ebx
  202698:      	jne	0x202775 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x225>
  20269e:      	testq	%r13, %r13
  2026a1:      	je	0x202775 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x225>
  2026a7:      	xorl	%r14d, %r14d
  2026aa:      	leaq	0x1fc(%rsp), %rbp
  2026b2:      	leaq	0x240(%rsp), %r15
  2026ba:      	nopw	(%rax,%rax)
  2026c0:      	movl	$0x100, %ebx            # imm = 0x100
  2026c5:      	movq	%r13, 0x90(%rsp)
  2026cd:      	nopl	(%rax)
  2026d0:      	movq	%r14, %rax
  2026d3:      	shlq	$0x4, %rax
  2026d7:      	movq	0x8(%r12,%rax), %rdx
  2026dc:      	movq	(%r12,%rax), %rsi
  2026e0:      	movq	0x2b0(%rsp), %rax
  2026e8:      	movq	%rbp, 0x68(%rsp)
  2026ed:      	movq	%rdx, %r13
  2026f0:      	subq	%rsi, %r13
  2026f3:      	movq	%r13, 0x70(%rsp)
  2026f8:      	movl	$0x20, 0x78(%rsp)
  202700:      	movq	0x78(%rsp), %rcx
  202705:      	movq	%r15, %rdi
  202708:      	movq	%rcx, 0x10(%rsp)
  20270d:      	vmovupd	0x68(%rsp), %xmm0
  202713:      	vmovupd	%xmm0, (%rsp)
  202718:      	callq	*%rax
  20271a:      	movq	%r14, %rax
  20271d:      	incq	%rax
  202720:      	movl	$0x0, %r14d
  202726:      	movl	0x1f8(%rsp,%r13,4), %ecx
  20272e:      	cmpq	$0x1000, %rax           # imm = 0x1000
  202734:      	cmovneq	%rax, %r14
  202738:      	movl	0x1fc(%rsp), %eax
  20273f:      	addq	%rax, %rcx
  202742:      	addq	0x88(%rsp), %rcx
  20274a:      	decq	%rbx
  20274d:      	movq	%rcx, 0x88(%rsp)
  202755:      	jne	0x2026d0 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x180>
  20275b:      	movq	0x90(%rsp), %r13
  202763:      	testq	%r13, %r13
  202766:      	jle	0x203017 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xac7>
  20276c:      	decq	%r13
  20276f:      	jne	0x2026c0 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x170>
  202775:      	movq	0x98(%rsp), %rbp
  20277d:      	movq	%rbp, %rdi
  202780:      	callq	0x64ab40 <benchmark::State::FinishKeepRunning()>

0000000000204280 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  2043c4:      	callq	0x64aab0 <benchmark::State::StartKeepRunning()>
  2043c9:      	testl	%ebx, %ebx
  2043cb:      	jne	0x2044a3 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x223>
  2043d1:      	testq	%r13, %r13
  2043d4:      	je	0x2044a3 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x223>
  2043da:      	xorl	%r14d, %r14d
  2043dd:      	leaq	0x350(%rsp), %rbp
  2043e5:      	leaq	0x1f0(%rsp), %r15
  2043ed:      	nopl	(%rax)
  2043f0:      	movl	$0x100, %ebx            # imm = 0x100
  2043f5:      	movq	%r13, 0x88(%rsp)
  2043fd:      	nopl	(%rax)
  204400:      	movq	%r14, %rax
  204403:      	shlq	$0x4, %rax
  204407:      	movq	0x8(%r12,%rax), %rdx
  20440c:      	movq	(%r12,%rax), %rsi
  204410:      	movq	0x260(%rsp), %rax
  204418:      	movq	%rbp, 0x60(%rsp)
  20441d:      	movq	%rdx, %r13
  204420:      	subq	%rsi, %r13
  204423:      	movq	%r13, 0x68(%rsp)
  204428:      	movl	$0x40, 0x70(%rsp)
  204430:      	movq	0x70(%rsp), %rcx
  204435:      	movq	%r15, %rdi
  204438:      	movq	%rcx, 0x10(%rsp)
  20443d:      	vmovupd	0x60(%rsp), %xmm0
  204443:      	vmovupd	%xmm0, (%rsp)
  204448:      	callq	*%rax
  20444a:      	movq	%r14, %rax
  20444d:      	incq	%rax
  204450:      	movl	$0x0, %r14d
  204456:      	movq	0x348(%rsp,%r13,8), %rcx
  20445e:      	cmpq	$0x1000, %rax           # imm = 0x1000
  204464:      	cmovneq	%rax, %r14
  204468:      	addq	0x350(%rsp), %rcx
  204470:      	addq	0x80(%rsp), %rcx
  204478:      	decq	%rbx
  20447b:      	movq	%rcx, 0x80(%rsp)
  204483:      	jne	0x204400 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x180>
  204489:      	movq	0x88(%rsp), %r13
  204491:      	testq	%r13, %r13
  204494:      	jle	0x204d1f <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xa9f>
  20449a:      	decq	%r13
  20449d:      	jne	0x2043f0 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x170>
  2044a3:      	movq	0x90(%rsp), %rbp
  2044ab:      	movq	%rbp, %rdi
  2044ae:      	callq	0x64ab40 <benchmark::State::FinishKeepRunning()>
