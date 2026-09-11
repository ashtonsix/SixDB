0000000000201350 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  201491:      	callq	0x6481c0 <benchmark::State::StartKeepRunning()>
  201496:      	testl	%ebx, %ebx
  201498:      	jne	0x201559 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x209>
  20149e:      	testq	%r15, %r15
  2014a1:      	je	0x201559 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x209>
  2014a7:      	xorl	%ebx, %ebx
  2014a9:      	leaq	0x1fc(%rsp), %r12
  2014b1:      	nopw	%cs:(%rax,%rax)
  2014c0:      	movq	%r15, 0x90(%rsp)
  2014c8:      	movl	$0x100, %r15d           # imm = 0x100
  2014ce:      	nop
  2014d0:      	movq	%rbx, %rax
  2014d3:      	shlq	$0x4, %rax
  2014d7:      	movq	(%r14,%rax), %r13
  2014db:      	movq	0x8(%r14,%rax), %rbp
  2014e0:      	leaq	0x240(%rsp), %rdi
  2014e8:      	movl	$0x20, %r8d
  2014ee:      	movq	%r13, %rsi
  2014f1:      	movq	%rbp, %rdx
  2014f4:      	movq	%r12, %rcx
  2014f7:      	callq	*0x2b0(%rsp)
  2014fe:      	movq	%rbx, %rax
  201501:      	incq	%rax
  201504:      	movl	$0x0, %ebx
  201509:      	notq	%r13
  20150c:      	cmpq	$0x1000, %rax           # imm = 0x1000
  201512:      	cmovneq	%rax, %rbx
  201516:      	addq	%r13, %rbp
  201519:      	movl	0x1fc(%rsp), %eax
  201520:      	movl	0x1fc(%rsp,%rbp,4), %ecx
  201527:      	addq	%rax, %rcx
  20152a:      	addq	0x88(%rsp), %rcx
  201532:      	decq	%r15
  201535:      	movq	%rcx, 0x88(%rsp)
  20153d:      	jne	0x2014d0 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x180>
  20153f:      	movq	0x90(%rsp), %r15
  201547:      	testq	%r15, %r15
  20154a:      	jle	0x201df7 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xaa7>
  201550:      	decq	%r15
  201553:      	jne	0x2014c0 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x170>
  201559:      	movq	0x98(%rsp), %rbp
  201561:      	movq	%rbp, %rdi
  201564:      	callq	0x648250 <benchmark::State::FinishKeepRunning()>

0000000000203020 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
  203164:      	callq	0x6481c0 <benchmark::State::StartKeepRunning()>
  203169:      	testl	%ebx, %ebx
  20316b:      	jne	0x203228 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x208>
  203171:      	testq	%r15, %r15
  203174:      	je	0x203228 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x208>
  20317a:      	xorl	%ebx, %ebx
  20317c:      	leaq	0x350(%rsp), %r12
  203184:      	nopw	%cs:(%rax,%rax)
  203190:      	movq	%r15, 0x88(%rsp)
  203198:      	movl	$0x100, %r15d           # imm = 0x100
  20319e:      	nop
  2031a0:      	movq	%rbx, %rax
  2031a3:      	shlq	$0x4, %rax
  2031a7:      	movq	(%r14,%rax), %r13
  2031ab:      	movq	0x8(%r14,%rax), %rbp
  2031b0:      	leaq	0x1f0(%rsp), %rdi
  2031b8:      	movl	$0x40, %r8d
  2031be:      	movq	%r13, %rsi
  2031c1:      	movq	%rbp, %rdx
  2031c4:      	movq	%r12, %rcx
  2031c7:      	callq	*0x260(%rsp)
  2031ce:      	movq	%rbx, %rax
  2031d1:      	incq	%rax
  2031d4:      	movl	$0x0, %ebx
  2031d9:      	notq	%r13
  2031dc:      	cmpq	$0x1000, %rax           # imm = 0x1000
  2031e2:      	cmovneq	%rax, %rbx
  2031e6:      	addq	%r13, %rbp
  2031e9:      	movq	0x350(%rsp,%rbp,8), %rax
  2031f1:      	addq	0x350(%rsp), %rax
  2031f9:      	addq	0x80(%rsp), %rax
  203201:      	decq	%r15
  203204:      	movq	%rax, 0x80(%rsp)
  20320c:      	jne	0x2031a0 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x180>
  20320e:      	movq	0x88(%rsp), %r15
  203216:      	testq	%r15, %r15
  203219:      	jle	0x203a9f <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0xa7f>
  20321f:      	decq	%r15
  203222:      	jne	0x203190 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x170>
  203228:      	movq	0x90(%rsp), %rbp
  203230:      	movq	%rbp, %rdi
  203233:      	callq	0x648250 <benchmark::State::FinishKeepRunning()>
