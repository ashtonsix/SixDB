0x950d60 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const>:
  950d60:      	pushq	%rbp
  950d61:      	pushq	%r15
  950d63:      	pushq	%r14
  950d65:      	pushq	%r13
  950d67:      	pushq	%r12
  950d69:      	pushq	%rbx
  950d6a:      	subq	$0x18, %rsp
  950d6e:      	movb	$0x28, %r10b
  950d71:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  950d7b:      	movl	$0x9, %ebp
  950d80:      	movl	$0xa, %r12d
  950d86:      	movq	%rdi, %r9
  950d89:      	xorl	%ebx, %ebx
  950d8b:      	xorl	%r14d, %r14d
  950d8e:      	xorl	%r15d, %r15d
  950d91:      	movq	%rdi, 0x10(%rsp)
  950d96:      	jmp	0x950dbc <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x5c>
  950d98:      	nopl	(%rax,%rax)
  950da0:      	xorl	%eax, %eax
  950da2:      	xorl	%r14d, %r14d
  950da5:      	movq	%r15, %rcx
  950da8:      	movq	%rax, %r15
  950dab:      	addq	%rcx, %r15
  950dae:      	incl	%ebx
  950db0:      	cmpl	$0x1c000, %ebx          # imm = 0x1C000
  950db6:      	je	0x951180 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x420>
  950dbc:      	movq	(%r9), %r13
  950dbf:      	movq	0x58(%r13), %rdi
  950dc3:      	movq	0x60(%r13), %rdx
  950dc7:      	movq	0x20(%r13), %r8
  950dcb:      	xorq	%r14, %rdi
  950dce:      	bzhiq	%r10, %rdi, %rsi
  950dd3:      	movq	%rsi, %rcx
  950dd6:      	shrq	$0xa, %rcx
  950dda:      	xorq	%rcx, %rdx
  950ddd:      	mulxq	%r11, %rdx, %rax
  950de2:      	xorl	%edx, %edi
  950de4:      	xorl	%eax, %edi
  950de6:      	shll	$0x7, %edi
  950de9:      	andl	$0x1ff80, %edi          # imm = 0x1FF80
  950def:      	leaq	(%r8,%rdi), %rax
  950df3:      	movzbl	(%r8,%rdi), %edi
  950df8:      	testb	%dil, %dil
  950dfb:      	je	0x950e89 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x129>
  950e01:      	movq	0x68(%r13), %rdx
  950e05:      	shrq	$0x12, %rsi
  950e09:      	vmovq	0x5(%rax), %xmm0
  950e0e:      	xorq	%rsi, %rdx
  950e11:      	mulxq	%r11, %r8, %rdx
  950e16:      	xorl	%r8d, %ecx
  950e19:      	movzwl	0xd(%rax), %r8d
  950e1e:      	xorl	%edx, %ecx
  950e20:      	movzbl	%dil, %edx
  950e24:      	cmpb	$0xa, %dil
  950e28:      	cmovael	%r12d, %edx
  950e2c:      	vmovd	%r8d, %xmm1
  950e31:      	vpunpcklqdq	%xmm1, %xmm0, %xmm0 # xmm0 = xmm0[0],xmm1[0]
  950e35:      	vmovd	%ecx, %xmm1
  950e39:      	vpbroadcastb	%xmm1, %xmm1
  950e3e:      	vpcmpeqb	%xmm0, %xmm1, %xmm0
  950e42:      	vpmovmskb	%xmm0, %ecx
  950e46:      	bzhil	%edx, %ecx, %edx
  950e4b:      	je	0x950e89 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x129>
  950e4d:      	nopl	(%rax)
  950e50:      	tzcntq	%rdx, %rcx
  950e55:      	leal	(%rcx,%rcx,2), %edi
  950e58:      	leal	0x78(,%rdi,8), %edi
  950e5f:      	shrl	$0x3, %edi
  950e62:      	movzwl	(%rax,%rdi), %r8d
  950e67:      	movzbl	0x2(%rax,%rdi), %edi
  950e6c:      	shll	$0x10, %edi
  950e6f:      	orl	%r8d, %edi
  950e72:      	cmpl	%esi, %edi
  950e74:      	je	0x95106b <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x30b>
  950e7a:      	movq	$-0x2, %rdi
  950e81:      	rolq	%cl, %rdi
  950e84:      	andq	%rdi, %rdx
  950e87:      	jne	0x950e50 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0xf0>
  950e89:      	cmpl	$0x0, 0x1(%rax)
  950e8d:      	je	0x950da0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x40>
  950e93:      	movq	0x78(%r13), %rdi
  950e97:      	movq	0x80(%r13), %rdx
  950e9e:      	movq	0x40(%r13), %r8
  950ea2:      	xorq	%r14, %rdi
  950ea5:      	bzhiq	%r10, %rdi, %rsi
  950eaa:      	movq	%rsi, %rcx
  950ead:      	shrq	$0x6, %rcx
  950eb1:      	xorq	%rcx, %rdx
  950eb4:      	mulxq	%r11, %rdx, %rax
  950eb9:      	xorl	%edx, %edi
  950ebb:      	xorl	%eax, %edi
  950ebd:      	andl	$0x3f, %edi
  950ec0:      	shll	$0x7, %edi
  950ec3:      	leaq	(%r8,%rdi), %rax
  950ec7:      	movzbl	(%r8,%rdi), %edi
  950ecc:      	testb	%dil, %dil
  950ecf:      	je	0x950f4b <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1eb>
  950ed1:      	movq	0x88(%r13), %rdx
  950ed8:      	shrq	$0xe, %rsi
  950edc:      	vmovq	0x9(%rax), %xmm0
  950ee1:      	xorq	%rsi, %rdx
  950ee4:      	mulxq	%r11, %r8, %rdx
  950ee9:      	xorl	%r8d, %ecx
  950eec:      	movzbl	0x11(%rax), %r8d
  950ef1:      	xorl	%edx, %ecx
  950ef3:      	movzbl	%dil, %edx
  950ef7:      	cmpb	$0x9, %dil
  950efb:      	cmovael	%ebp, %edx
  950efe:      	vmovd	%r8d, %xmm1
  950f03:      	vpunpcklqdq	%xmm1, %xmm0, %xmm0 # xmm0 = xmm0[0],xmm1[0]
  950f07:      	vmovd	%ecx, %xmm1
  950f0b:      	vpbroadcastb	%xmm1, %xmm1
  950f10:      	vpcmpeqb	%xmm0, %xmm1, %xmm0
  950f14:      	vpmovmskb	%xmm0, %ecx
  950f18:      	bzhil	%edx, %ecx, %edx
  950f1d:      	je	0x950f4b <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1eb>
  950f1f:      	nop
  950f20:      	tzcntq	%rdx, %rcx
  950f25:      	movl	%ecx, %edi
  950f27:      	shll	$0x5, %edi
  950f2a:      	addl	$0x90, %edi
  950f30:      	shrl	$0x3, %edi
  950f33:      	cmpl	(%rax,%rdi), %esi
  950f36:      	je	0x9510e6 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x386>
  950f3c:      	movq	$-0x2, %rdi
  950f43:      	rolq	%cl, %rdi
  950f46:      	andq	%rdi, %rdx
  950f49:      	jne	0x950f20 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x1c0>
  950f4b:      	cmpl	$0x0, 0x1(%rax)
  950f4f:      	je	0x950da0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x40>
  950f55:      	movl	%ebx, 0x8(%rsp)
  950f59:      	movl	0x5(%rax), %ebx
  950f5c:      	testl	%ebx, %ebx
  950f5e:      	jne	0x950f7c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x21c>
  950f60:      	movl	0x8(%rsp), %ebx
  950f64:      	jmp	0x950da0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x40>
  950f69:      	nopl	(%rax)
  950f70:      	movl	0x2(%rax,%rcx), %ebx
  950f74:      	testl	%ebx, %ebx
  950f76:      	je	0x951105 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3a5>
  950f7c:      	movq	0xa0(%r13), %r12
  950f83:      	movzbl	0xab13a6(%rip), %eax    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  950f8a:      	testb	%al, %al
  950f8c:      	je	0x95101b <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2bb>
  950f92:      	movl	%ebx, %ebp
  950f94:      	shrl	$0x8, %ebp
  950f97:      	movq	0xab137a(%rip), %rdx    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  950f9e:      	decl	%ebp
  950fa0:      	leaq	(%rbp,%rbp,4), %rax
  950fa5:      	movl	0x20(%r12,%rax,8), %ecx
  950faa:      	movq	0x8(%r12,%rax,8), %rax
  950faf:      	imulq	$0x2c, %rcx, %rcx
  950fb3:      	movzwl	0x8(%rdx,%rcx), %esi
  950fb8:      	cmpb	$0x0, 0xa(%rdx,%rcx)
  950fbd:      	movl	$0x0, %ecx
  950fc2:      	cmovel	%ecx, %esi
  950fc5:      	movzbl	%bl, %ecx
  950fc8:      	imulq	%rsi, %rcx
  950fcc:      	movzwl	0x6(%rax,%rcx), %edx
  950fd1:      	testq	%rdx, %rdx
  950fd4:      	je	0x950f70 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x210>
  950fd6:      	leaq	(%rdx,%rdx,2), %rsi
  950fda:      	movq	$-0xd, %r12
  950fe1:      	leaq	(%rdx,%rsi,4), %rdx
  950fe5:      	leaq	0xc(%rax,%rcx), %rsi
  950fea:      	nopw	(%rax,%rax)
  950ff0:      	movzbl	(%rsi), %r8d
  950ff4:      	movl	-0x4(%rsi), %edi
  950ff7:      	shlq	$0x20, %r8
  950ffb:      	orq	%rdi, %r8
  950ffe:      	cmpq	%r14, %r8
  951001:      	je	0x951076 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x316>
  951003:      	leaq	-0xd(%rdx,%r12), %rdi
  951008:      	addq	$-0xd, %r12
  95100c:      	addq	$0xd, %rsi
  951010:      	cmpq	$-0xd, %rdi
  951014:      	jne	0x950ff0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x290>
  951016:      	jmp	0x950f70 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x210>
  95101b:      	leaq	0xab130e(%rip), %rdi    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  951022:      	callq	0x7780 <__cxa_guard_acquire@plt>
  951027:      	testl	%eax, %eax
  951029:      	je	0x950f92 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x232>
  95102f:      	leaq	0xab12e2(%rip), %rdi    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  951036:      	leaq	0xe(%rsp), %rsi
  95103b:      	callq	0x94c1b0 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  951040:      	leaq	-0x940797(%rip), %rdi   # 0x108b0 <std::vector<ikea::streampack::layout, std::allocator<ikea::streampack::layout>>::~vector()>
  951047:      	leaq	0xab12ca(%rip), %rsi    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  95104e:      	leaq	0xab03ab(%rip), %rdx    # 0x1401400 <__dso_handle>
  951055:      	callq	0x7390 <__cxa_atexit@plt>
  95105a:      	leaq	0xab12cf(%rip), %rdi    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  951061:      	callq	0x72b0 <__cxa_guard_release@plt>
  951066:      	jmp	0x950f92 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x232>
  95106b:      	shll	$0x6, %ecx
  95106e:      	addl	$0x168, %ecx            # imm = 0x168
  951074:      	jmp	0x9510ef <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x38f>
  951076:      	movq	0xa0(%r13), %r14
  95107d:      	movzbl	0xab12ac(%rip), %eax    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  951084:      	testb	%al, %al
  951086:      	je	0x951130 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3d0>
  95108c:      	leaq	(%rbp,%rbp,4), %rcx
  951091:      	movq	0xab1280(%rip), %rdx    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  951098:      	movq	0x10(%rsp), %r9
  95109d:      	movb	$0x28, %r10b
  9510a0:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  9510aa:      	movl	$0x9, %ebp
  9510af:      	movl	0x20(%r14,%rcx,8), %eax
  9510b4:      	imulq	$0x2c, %rax, %rax
  9510b8:      	movzwl	0x8(%rdx,%rax), %esi
  9510bd:      	cmpb	$0x0, 0xa(%rdx,%rax)
  9510c2:      	movl	$0x0, %eax
  9510c7:      	cmoveq	%rax, %rsi
  9510cb:      	movzbl	%bl, %eax
  9510ce:      	movl	0x8(%rsp), %ebx
  9510d2:      	imulq	%rsi, %rax
  9510d6:      	addq	0x8(%r14,%rcx,8), %rax
  9510db:      	subq	%r12, %rax
  9510de:      	movl	$0xa, %r12d
  9510e4:      	jmp	0x9510f5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x395>
  9510e6:      	shll	$0x6, %ecx
  9510e9:      	addl	$0x1b0, %ecx            # imm = 0x1B0
  9510ef:      	shrl	$0x3, %ecx
  9510f2:      	addq	%rcx, %rax
  9510f5:      	movq	(%rax), %r14
  9510f8:      	movl	$0x1, %eax
  9510fd:      	addq	%r14, %r15
  951100:      	jmp	0x950da5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x45>
  951105:      	movq	0x10(%rsp), %r9
  95110a:      	movl	0x8(%rsp), %ebx
  95110e:      	xorl	%eax, %eax
  951110:      	xorl	%r14d, %r14d
  951113:      	movb	$0x28, %r10b
  951116:      	movabsq	$0x79d5f9e0de1e8cf5, %r11 # imm = 0x79D5F9E0DE1E8CF5
  951120:      	movl	$0x9, %ebp
  951125:      	movl	$0xa, %r12d
  95112b:      	jmp	0x950da5 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x45>
  951130:      	leaq	0xab11f9(%rip), %rdi    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  951137:      	callq	0x7780 <__cxa_guard_acquire@plt>
  95113c:      	testl	%eax, %eax
  95113e:      	je	0x95108c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x32c>
  951144:      	leaq	0xab11cd(%rip), %rdi    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  95114b:      	leaq	0xf(%rsp), %rsi
  951150:      	callq	0x94c1b0 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  951155:      	leaq	-0x9408ac(%rip), %rdi   # 0x108b0 <std::vector<ikea::streampack::layout, std::allocator<ikea::streampack::layout>>::~vector()>
  95115c:      	leaq	0xab11b5(%rip), %rsi    # 0x1402318 <ht::links<40u, false, 8u, false>::formats()::p>
  951163:      	leaq	0xab0296(%rip), %rdx    # 0x1401400 <__dso_handle>
  95116a:      	callq	0x7390 <__cxa_atexit@plt>
  95116f:      	leaq	0xab11ba(%rip), %rdi    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  951176:      	callq	0x72b0 <__cxa_guard_release@plt>
  95117b:      	jmp	0x95108c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x32c>
  951180:      	movq	%r15, %rax
  951183:      	addq	$0x18, %rsp
  951187:      	popq	%rbx
  951188:      	popq	%r12
  95118a:      	popq	%r13
  95118c:      	popq	%r14
  95118e:      	popq	%r15
  951190:      	popq	%rbp
  951191:      	retq
  951192:      	jmp	0x951194 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x434>
  951194:      	leaq	0xab1195(%rip), %rdi    # 0x1402330 <guard variable for ht::links<40u, false, 8u, false>::formats()::p>
  95119b:      	movq	%rax, %rbx
  95119e:      	callq	0x7290 <__cxa_guard_abort@plt>
  9511a3:      	movq	%rbx, %rdi
  9511a6:      	callq	0x7740 <_Unwind_Resume@plt>
  9511ab:      	nopl	(%rax,%rax)

