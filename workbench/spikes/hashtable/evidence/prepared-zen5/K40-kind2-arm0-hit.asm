Phase resolved from ['./build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:131']
Complete timed CFG, address order.
957436: movq %rax, 0x38(%rsp)
95743b: movq 0x8(%rbx), %rax
95743f: movq %rbx, 0x30(%rsp)
957444: movq (%rax), %rcx
957447: movq (%rcx), %rdx
95744a: movq 0x8(%rcx), %r13
95744e: movq %rdx, 0x40(%rsp)
957453: cmpq %r13, %rdx
957456: je 0x9578e8 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x4c8>
95745c: movq 0x8(%rax), %r8
957460: movabsq $0xb86659fd93, %r12     # imm = 0xB86659FD93
95746a: xorl %eax, %eax
95746c: xorl %ebx, %ebx
95746e: movq %r13, 0x28(%rsp)
957473: movq 0x20(%r8), %rdi
957477: movq 0x58(%r8), %r9
95747b: movq 0x60(%r8), %r11
95747f: movq %rdi, 0x18(%rsp)
957484: movq %r9, 0x10(%rsp)
957489: movq %r11, 0x8(%rsp)
95748e: jmp 0x95749f <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x7f>
957490: movl 0x24(%rsp), %eax
957494: incl %eax
957496: cmpl $0x10, %eax
957499: je 0x9578ea <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x4ca>
95749f: movq 0x40(%rsp), %rbp
9574a4: movl %eax, 0x24(%rsp)
9574a8: jmp 0x9574c4 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0xa4>
9574b0: xorl %eax, %eax
9574b2: movq %rbx, %rcx
9574b5: movq %rax, %rbx
9574b8: addq %rcx, %rbx
9574bb: addq $0x4, %rbp
9574bf: cmpq %r13, %rbp
9574c2: je 0x957490 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x70>
9574c4: movl (%rbp), %eax
9574c7: movb $0x28, %cl
9574c9: movabsq $0x79d5f9e0de1e8cf5, %r10 # imm = 0x79D5F9E0DE1E8CF5
9574d3: imulq %r12, %rax
9574d7: movq %rax, %rsi
9574da: xorq %r9, %rsi
9574dd: bzhiq %rcx, %rsi, %r14
9574e2: movq %r14, %rcx
9574e5: shrq $0xa, %rcx
9574e9: movq %rcx, %rdx
9574ec: xorq %r11, %rdx
9574ef: mulxq %r10, %r10, %rdx
9574f4: xorl %r10d, %esi
9574f7: xorl %edx, %esi
9574f9: shll $0x7, %esi
9574fc: andl $0x1ff80, %esi          # imm = 0x1FF80
957502: leaq (%rdi,%rsi), %r15
957506: movzbl (%rdi,%rsi), %esi
95750a: testb %sil, %sil
95750d: je 0x9575a3 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x183>
957513: movq 0x68(%r8), %rdx
957517: shrq $0x12, %r14
95751b: movabsq $0x79d5f9e0de1e8cf5, %r10 # imm = 0x79D5F9E0DE1E8CF5
957525: vmovq 0x5(%r15), %xmm0
95752b: xorq %r14, %rdx
95752e: mulxq %r10, %r10, %rdx
957533: xorl %r10d, %ecx
957536: movzbl 0xd(%r15), %r10d
95753b: xorl %edx, %ecx
95753d: movzbl %sil, %edx
957541: cmpb $0x9, %sil
957545: movl $0x9, %esi
95754a: cmovael %esi, %edx
95754d: vmovd %r10d, %xmm1
957552: vpunpcklqdq %xmm1, %xmm0, %xmm0 # xmm0 = xmm0[0],xmm1[0]
957556: vmovd %ecx, %xmm1
95755a: vpbroadcastb %xmm1, %xmm1
95755f: vpcmpeqb %xmm0, %xmm1, %xmm0
957563: vpmovmskb %xmm0, %ecx
957567: bzhil %edx, %ecx, %edx
95756c: je 0x9575a3 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x183>
95756e: nop 
957570: tzcntq %rdx, %rcx
957575: leaq (%rcx,%rcx,2), %rsi
957579: movzwl 0xe(%r15,%rsi), %r10d
95757f: movzbl 0x10(%r15,%rsi), %esi
957585: shll $0x10, %esi
957588: orl %r10d, %esi
95758b: cmpl %r14d, %esi
95758e: je 0x957728 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x308>
957594: movq $-0x2, %rsi
95759b: rolq %cl, %rsi
95759e: andq %rsi, %rdx
9575a1: jne 0x957570 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x150>
9575a3: cmpl $0x0, 0x1(%r15)
9575a8: je 0x9574b0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x90>
9575ae: movq 0x78(%r8), %rsi
9575b2: movq 0x80(%r8), %rdx
9575b9: movb $0x28, %cl
9575bb: movabsq $0x79d5f9e0de1e8cf5, %r10 # imm = 0x79D5F9E0DE1E8CF5
9575c5: xorq %rax, %rsi
9575c8: bzhiq %rcx, %rsi, %r14
9575cd: movq %r14, %rcx
9575d0: shrq $0x6, %rcx
9575d4: xorq %rcx, %rdx
9575d7: mulxq %r10, %r10, %rdx
9575dc: xorl %r10d, %esi
9575df: movq 0x40(%r8), %r10
9575e3: xorl %edx, %esi
9575e5: andl $0x3f, %esi
9575e8: shll $0x7, %esi
9575eb: leaq (%r10,%rsi), %r15
9575ef: movzbl (%r10,%rsi), %esi
9575f4: testb %sil, %sil
9575f7: je 0x95766f <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x24f>
9575f9: movq 0x88(%r8), %rdx
957600: shrq $0xe, %r14
957604: movabsq $0x79d5f9e0de1e8cf5, %r10 # imm = 0x79D5F9E0DE1E8CF5
95760e: vmovq 0x9(%r15), %xmm0
957614: xorq %r14, %rdx
957617: mulxq %r10, %r10, %rdx
95761c: xorl %r10d, %ecx
95761f: xorl %edx, %ecx
957621: movzbl %sil, %edx
957625: cmpb $0x8, %sil
957629: movl $0x8, %esi
95762e: vmovd %ecx, %xmm1
957632: cmovael %esi, %edx
957635: vpbroadcastb %xmm1, %xmm1
95763a: vpcmpeqb %xmm0, %xmm1, %xmm0
95763e: vpmovmskb %xmm0, %ecx
957642: bzhil %edx, %ecx, %edx
957647: je 0x95766f <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x24f>
957649: nopl (%rax)
957650: tzcntq %rdx, %rcx
957655: cmpl 0x11(%r15,%rcx,4), %r14d
95765a: je 0x9578a4 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x484>
957660: movq $-0x2, %rsi
957667: rolq %cl, %rsi
95766a: andq %rsi, %rdx
95766d: jne 0x957650 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x230>
95766f: cmpl $0x0, 0x1(%r15)
957674: je 0x9574b0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x90>
95767a: movl 0x5(%r15), %esi
95767e: testl %esi, %esi
957680: je 0x9574b0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x90>
957686: movb $0x28, %cl
957688: movq 0xa8(%r8), %rdx
95768f: bzhiq %rcx, %rax, %rax
957694: movq 0xa0(%r8), %rcx
95769b: jmp 0x9576ad <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x28d>
9576a0: movl 0x2(%r15,%r10), %esi
9576a5: testl %esi, %esi
9576a7: je 0x9578c3 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x4a3>
9576ad: movl %esi, %r10d
9576b0: shrl $0x8, %r10d
9576b4: movzbl %sil, %esi
9576b8: decl %r10d
9576bb: leaq (%r10,%r10,4), %r10
9576bf: movl 0x20(%rdx,%r10,8), %r11d
9576c4: movq 0x8(%rdx,%r10,8), %r15
9576c9: movq %rsi, %r10
9576cc: imulq $0x2c, %r11, %r11
9576d0: movzbl 0xa(%rcx,%r11), %r12d
9576d6: movzwl 0x8(%rcx,%r11), %r14d
9576dc: movl $0x0, %r11d
9576e2: testb %r12b, %r12b
9576e5: cmovnel %r14d, %r11d
9576e9: imulq %r11, %r10
9576ed: movzwl 0x6(%r15,%r10), %r13d
9576f3: testq %r13, %r13
9576f6: je 0x9576a0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x280>
9576f8: leaq 0xc(%r15), %r11
9576fc: nopl (%rax)
957700: movzbl (%r11,%r10), %edi
957705: movl -0x4(%r11,%r10), %r9d
95770a: shlq $0x20, %rdi
95770e: orq %r9, %rdi
957711: cmpq %rax, %rdi
957714: je 0x957851 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x431>
95771a: addq $0xe, %r11
95771e: decq %r13
957721: jne 0x957700 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x2e0>
957723: jmp 0x9576a0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x280>
957728: leaq (%rcx,%rcx,8), %rcx
95772c: leaq 0x29(%r15,%rcx), %rax
957731: movzbl 0x31(%r15,%rcx), %ecx
957737: andb $0x1, %cl
95773a: movq (%rax), %rsi
95773d: testb %cl, %cl
95773f: je 0x957897 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x477>
957745: movl $0x1, %eax
95774a: testl %esi, %esi
95774c: je 0x9574b2 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x92>
957752: movq 0x118(%r8), %rdx
957759: movq 0x8(%rdx), %rcx
95775d: movq 0x10(%rdx), %rdx
957761: jmp 0x95777d <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x35d>
957770: movl 0x2(%r14,%r15), %esi
957775: testl %esi, %esi
957777: je 0x9578ca <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x4aa>
95777d: movl %esi, %edi
95777f: shrl $0x8, %edi
957782: movzbl %sil, %r15d
957786: decl %edi
957788: leaq (%rdi,%rdi,4), %rdi
95778c: movl 0x20(%rdx,%rdi,8), %r9d
957791: movq 0x8(%rdx,%rdi,8), %r14
957796: imulq $0x2c, %r9, %r9
95779a: movzwl 0x8(%rcx,%r9), %edi
9577a0: cmpb $0x0, 0xa(%rcx,%r9)
9577a6: movl $0x0, %r9d
9577ac: cmovel %r9d, %edi
9577b0: imulq %rdi, %r15
9577b4: movzwl 0x6(%r14,%r15), %r12d
9577ba: testq %r12, %r12
9577bd: je 0x957770 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x350>
9577bf: cmpw $0x8, %r12w
9577c4: jae 0x9577d0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x3b0>
9577c6: xorl %esi, %esi
9577c8: jmp 0x957820 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x400>
9577d0: leaq 0x2(%r14,%r15), %r10
9577d5: movl %r12d, %r11d
9577d8: andl $-0x8, %r11d
9577dc: xorl %esi, %esi
9577de: addq $0x6, %r10
9577e2: addq $0x38, %r10
9577e6: nopw %cs:(%rax,%rax)
9577f0: addq -0x38(%r10,%rsi,8), %rbx
9577f5: addq -0x30(%r10,%rsi,8), %rbx
9577fa: addq -0x28(%r10,%rsi,8), %rbx
9577ff: addq -0x20(%r10,%rsi,8), %rbx
957804: addq -0x18(%r10,%rsi,8), %rbx
957809: addq -0x10(%r10,%rsi,8), %rbx
95780e: addq -0x8(%r10,%rsi,8), %rbx
957813: addq (%r10,%rsi,8), %rbx
957817: addq $0x8, %rsi
95781b: cmpq %rsi, %r11
95781e: jne 0x9577f0 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x3d0>
957820: andl $0x7, %r12d
957824: je 0x957770 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x350>
95782a: leaq (%r15,%rsi,8), %rsi
95782e: xorl %r10d, %r10d
957831: leaq 0x8(%r14,%rsi), %rsi
957836: nopw %cs:(%rax,%rax)
957840: addq (%rsi,%r10,8), %rbx
957844: incq %r10
957847: cmpq %r10, %r12
95784a: jne 0x957840 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x420>
95784c: jmp 0x957770 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x350>
957851: testb %r12b, %r12b
957854: movl $0x0, %eax
957859: movl %esi, %ecx
95785b: movq 0x28(%rsp), %r13
957860: movq 0x18(%rsp), %rdi
957865: movq 0x10(%rsp), %r9
95786a: movabsq $0xb86659fd93, %r12     # imm = 0xB86659FD93
957874: cmoveq %rax, %r14
957878: imulq %r14, %rcx
95787c: leaq 0x1(%r11,%rcx), %rax
957881: movzbl 0x9(%r11,%rcx), %ecx
957887: movq 0x8(%rsp), %r11
95788c: movq (%rax), %rsi
95788f: testb %cl, %cl
957891: jne 0x957745 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x325>
957897: addq %rsi, %rbx
95789a: movl $0x1, %eax
95789f: jmp 0x9574b2 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x92>
9578a4: leaq (%rcx,%rcx,8), %rcx
9578a8: leaq 0x31(%r15,%rcx), %rax
9578ad: movzbl 0x39(%r15,%rcx), %ecx
9578b3: andb $0x1, %cl
9578b6: movq (%rax), %rsi
9578b9: testb %cl, %cl
9578bb: jne 0x957745 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x325>
9578c1: jmp 0x957897 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x477>
9578c3: movq 0x28(%rsp), %r13
9578c8: xorl %eax, %eax
9578ca: movq 0x18(%rsp), %rdi
9578cf: movq 0x10(%rsp), %r9
9578d4: movq 0x8(%rsp), %r11
9578d9: movabsq $0xb86659fd93, %r12     # imm = 0xB86659FD93
9578e3: jmp 0x9574b2 <double ht::format_time<void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()>(void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda1'()&&)+0x92>
9578e8: xorl %ebx, %ebx
9578ea: movq 0x30(%rsp), %rax
9578ef: movq (%rax), %rax
9578f2: movq %rbx, (%rax)
