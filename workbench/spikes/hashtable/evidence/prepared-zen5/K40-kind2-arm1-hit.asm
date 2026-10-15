Phase resolved from ['./build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:67', './build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:131']
Complete timed CFG, address order.
965f0a: movq 0xa0(%rsp), %rdx
965f12: movq %rax, 0x48(%rsp)
965f17: xorl %r9d, %r9d
965f1a: movq (%rdx), %rcx
965f1d: movq 0x8(%rdx), %rax
965f21: movq %rcx, 0x38(%rsp)
965f26: movq %rax, 0x8(%rsp)
965f2b: cmpq %rax, %rcx
965f2e: je 0x9663f8 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3408>
965f34: movq 0xa8(%rsp), %r8
965f3c: movb $0x28, %r11b
965f3f: xorl %r12d, %r12d
965f42: movq 0x20(%r8), %rax
965f46: movq %r8, 0x20(%rsp)
965f4b: movq %rax, 0x18(%rsp)
965f50: movq 0x58(%r8), %rax
965f54: movq %rax, 0x10(%rsp)
965f59: movq 0x60(%r8), %rax
965f5d: movq %rax, 0x30(%rsp)
965f62: xorl %eax, %eax
965f64: jmp 0x965f7f <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2f8f>
965f70: movl 0x28(%rsp), %eax
965f74: incl %eax
965f76: cmpl $0x10, %eax
965f79: je 0x9663fb <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x340b>
965f7f: movq 0x38(%rsp), %r13
965f84: movl %eax, 0x28(%rsp)
965f88: jmp 0x965fa6 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fb6>
965f90: xorl %eax, %eax
965f92: movq %r12, %rcx
965f95: movq %rax, %r12
965f98: addq %rcx, %r12
965f9b: addq $0x4, %r13
965f9f: cmpq 0x8(%rsp), %r13
965fa4: je 0x965f70 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2f80>
965fa6: movl (%r13), %eax
965faa: movabsq $0xb86659fd93, %rcx     # imm = 0xB86659FD93
965fb4: movabsq $0x79d5f9e0de1e8cf5, %rdi # imm = 0x79D5F9E0DE1E8CF5
965fbe: imulq %rcx, %rax
965fc2: movq %rax, %rsi
965fc5: xorq 0x10(%rsp), %rsi
965fca: bzhiq %r11, %rsi, %rbp
965fcf: movq %rbp, %rcx
965fd2: shrq $0xa, %rcx
965fd6: movq %rcx, %rdx
965fd9: xorq 0x30(%rsp), %rdx
965fde: mulxq %rdi, %r10, %rdx
965fe3: xorl %r10d, %esi
965fe6: movq 0x18(%rsp), %r10
965feb: xorl %edx, %esi
965fed: shll $0x7, %esi
965ff0: andl $0x1ff80, %esi          # imm = 0x1FF80
965ff6: leaq (%r10,%rsi), %rbx
965ffa: movzbl (%r10,%rsi), %esi
965fff: testb %sil, %sil
966002: je 0x9660ef <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x30ff>
966008: movq 0x68(%r8), %rdx
96600c: shrq $0x12, %rbp
966010: xorq %rbp, %rdx
966013: mulxq %rdi, %r10, %rdx
966018: xorl %r10d, %ecx
96601b: movq 0x5(%rbx), %r10
96601f: xorl %edx, %ecx
966021: movzbl %sil, %edx
966025: cmpb $0x9, %sil
966029: movl $0x9, %esi
96602e: cmovael %esi, %edx
966031: vmovd %ecx, %xmm1
966035: vpbroadcastb %xmm1, %xmm1
96603a: movl %r10d, %edi
96603d: movzbl %r10b, %r8d
966041: shrl $0x8, %edi
966044: movl %r10d, %r9d
966047: shrl $0x10, %r9d
96604b: movl %r10d, %r15d
96604e: shrl $0x18, %r15d
966052: movq %r10, %r14
966055: shrq $0x20, %r14
966059: movq %r10, %r11
96605c: shrq $0x28, %r11
966060: movq %r10, %rsi
966063: shrq $0x30, %rsi
966067: shrq $0x38, %r10
96606b: vmovd %r8d, %xmm0
966070: movq 0x20(%rsp), %r8
966075: vpinsrb $0x1, %edi, %xmm0, %xmm0
96607b: vpinsrb $0x2, %r9d, %xmm0, %xmm0
966081: xorl %r9d, %r9d
966084: vpinsrb $0x3, %r15d, %xmm0, %xmm0
96608a: vpinsrb $0x4, %r14d, %xmm0, %xmm0
966090: vpinsrb $0x5, %r11d, %xmm0, %xmm0
966096: movb $0x28, %r11b
966099: vpinsrb $0x6, %esi, %xmm0, %xmm0
96609f: vpinsrb $0x7, %r10d, %xmm0, %xmm0
9660a5: vpinsrb $0x8, 0xd(%rbx), %xmm0, %xmm0
9660ac: vpcmpeqb %xmm1, %xmm0, %xmm0
9660b0: vpmovmskb %xmm0, %ecx
9660b4: bzhil %edx, %ecx, %edx
9660b9: je 0x9660ef <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x30ff>
9660bb: nopl (%rax,%rax)
9660c0: tzcntq %rdx, %rcx
9660c5: leaq (%rcx,%rcx,2), %rsi
9660c9: movzwl 0xe(%rbx,%rsi), %edi
9660ce: movzbl 0x10(%rbx,%rsi), %esi
9660d3: shll $0x10, %esi
9660d6: orl %edi, %esi
9660d8: cmpl %esi, %ebp
9660da: je 0x966268 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3278>
9660e0: movq $-0x2, %rsi
9660e7: rolq %cl, %rsi
9660ea: andq %rsi, %rdx
9660ed: jne 0x9660c0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x30d0>
9660ef: cmpl $0x0, 0x1(%rbx)
9660f3: je 0x965f90 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa0>
9660f9: movq 0x78(%r8), %rsi
9660fd: movq 0x80(%r8), %rdx
966104: movabsq $0x79d5f9e0de1e8cf5, %rdi # imm = 0x79D5F9E0DE1E8CF5
96610e: xorq %rax, %rsi
966111: bzhiq %r11, %rsi, %rbx
966116: movq %rbx, %rcx
966119: shrq $0x6, %rcx
96611d: xorq %rcx, %rdx
966120: mulxq %rdi, %rdi, %rdx
966125: xorl %edi, %esi
966127: movq 0x40(%r8), %rdi
96612b: xorl %edx, %esi
96612d: andl $0x3f, %esi
966130: shll $0x7, %esi
966133: leaq (%rdi,%rsi), %r15
966137: movzbl (%rdi,%rsi), %esi
96613b: testb %sil, %sil
96613e: je 0x9661af <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31bf>
966140: movq 0x88(%r8), %rdx
966147: shrq $0xe, %rbx
96614b: movabsq $0x79d5f9e0de1e8cf5, %rdi # imm = 0x79D5F9E0DE1E8CF5
966155: vmovq 0x9(%r15), %xmm0
96615b: xorq %rbx, %rdx
96615e: mulxq %rdi, %rdi, %rdx
966163: xorl %edi, %ecx
966165: xorl %edx, %ecx
966167: movzbl %sil, %edx
96616b: cmpb $0x8, %sil
96616f: movl $0x8, %esi
966174: vmovd %ecx, %xmm1
966178: cmovael %esi, %edx
96617b: vpbroadcastb %xmm1, %xmm1
966180: vpcmpeqb %xmm0, %xmm1, %xmm0
966184: vpmovmskb %xmm0, %ecx
966188: bzhil %edx, %ecx, %edx
96618d: je 0x9661af <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31bf>
96618f: nop 
966190: tzcntq %rdx, %rcx
966195: cmpl 0x11(%r15,%rcx,4), %ebx
96619a: je 0x9663ca <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x33da>
9661a0: movq $-0x2, %rsi
9661a7: rolq %cl, %rsi
9661aa: andq %rsi, %rdx
9661ad: jne 0x966190 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31a0>
9661af: cmpl $0x0, 0x1(%r15)
9661b4: je 0x965f90 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa0>
9661ba: movl 0x5(%r15), %esi
9661be: testl %esi, %esi
9661c0: je 0x965f90 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa0>
9661c6: movq 0xa0(%r8), %rcx
9661cd: movq 0xa8(%r8), %rdx
9661d4: bzhiq %r11, %rax, %rax
9661d9: jmp 0x9661ed <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31fd>
9661e0: movl 0x2(%r15,%rsi), %esi
9661e5: testl %esi, %esi
9661e7: je 0x9663e9 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x33f9>
9661ed: movl %esi, %edi
9661ef: shrl $0x8, %edi
9661f2: movzbl %sil, %r14d
9661f6: decl %edi
9661f8: movq %r14, %rsi
9661fb: leaq (%rdi,%rdi,4), %rdi
9661ff: movl 0x20(%rdx,%rdi,8), %r8d
966204: movq 0x8(%rdx,%rdi,8), %r15
966209: movl $0x0, %edi
96620e: imulq $0x2c, %r8, %r8
966212: movzbl 0xa(%rcx,%r8), %ebp
966218: movzwl 0x8(%rcx,%r8), %ebx
96621e: testb %bpl, %bpl
966221: cmovnel %ebx, %edi
966224: imulq %rdi, %rsi
966228: movzwl 0x6(%r15,%rsi), %r11d
96622e: testq %r11, %r11
966231: je 0x9661e0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31f0>
966233: leaq 0xc(%r15), %r10
966237: nopw (%rax,%rax)
966240: movzbl (%r10,%rsi), %r8d
966245: movl -0x4(%r10,%rsi), %edi
96624a: shlq $0x20, %r8
96624e: orq %rdi, %r8
966251: cmpq %rax, %r8
966254: je 0x966391 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x33a1>
96625a: addq $0xe, %r10
96625e: decq %r11
966261: jne 0x966240 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3250>
966263: jmp 0x9661e0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x31f0>
966268: leaq (%rcx,%rcx,8), %rcx
96626c: leaq 0x29(%rbx,%rcx), %rax
966271: movzbl 0x31(%rbx,%rcx), %ecx
966276: andb $0x1, %cl
966279: movq (%rax), %r14
96627c: testb %cl, %cl
96627e: je 0x9663bd <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x33cd>
966284: movl $0x1, %eax
966289: testl %r14d, %r14d
96628c: je 0x965f92 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa2>
966292: movq 0x118(%r8), %rdx
966299: movq 0x8(%rdx), %rcx
96629d: movq 0x10(%rdx), %rdx
9662a1: jmp 0x9662be <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x32ce>
9662b0: movl 0x2(%rbx,%rbp), %r14d
9662b5: testl %r14d, %r14d
9662b8: je 0x965f92 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa2>
9662be: movl %r14d, %esi
9662c1: shrl $0x8, %esi
9662c4: movzbl %r14b, %ebp
9662c8: decl %esi
9662ca: leaq (%rsi,%rsi,4), %rsi
9662ce: movl 0x20(%rdx,%rsi,8), %edi
9662d2: movq 0x8(%rdx,%rsi,8), %rbx
9662d7: imulq $0x2c, %rdi, %rdi
9662db: movzwl 0x8(%rcx,%rdi), %esi
9662e0: cmpb $0x0, 0xa(%rcx,%rdi)
9662e5: cmovel %r9d, %esi
9662e9: imulq %rsi, %rbp
9662ed: movzwl 0x6(%rbx,%rbp), %r15d
9662f3: testq %r15, %r15
9662f6: je 0x9662b0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x32c0>
9662f8: cmpw $0x8, %r15w
9662fd: jae 0x966310 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3320>
9662ff: xorl %r14d, %r14d
966302: jmp 0x966360 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3370>
966310: leaq 0x2(%rbx,%rbp), %rsi
966315: movl %r15d, %r10d
966318: andl $-0x8, %r10d
96631c: xorl %r14d, %r14d
96631f: addq $0x6, %rsi
966323: addq $0x38, %rsi
966327: nopw (%rax,%rax)
966330: addq -0x38(%rsi,%r14,8), %r12
966335: addq -0x30(%rsi,%r14,8), %r12
96633a: addq -0x28(%rsi,%r14,8), %r12
96633f: addq -0x20(%rsi,%r14,8), %r12
966344: addq -0x18(%rsi,%r14,8), %r12
966349: addq -0x10(%rsi,%r14,8), %r12
96634e: addq -0x8(%rsi,%r14,8), %r12
966353: addq (%rsi,%r14,8), %r12
966357: addq $0x8, %r14
96635b: cmpq %r14, %r10
96635e: jne 0x966330 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3340>
966360: andl $0x7, %r15d
966364: je 0x9662b0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x32c0>
96636a: leaq (%rbp,%r14,8), %rsi
96636f: xorl %r10d, %r10d
966372: leaq 0x8(%rbx,%rsi), %rsi
966377: nopw (%rax,%rax)
966380: addq (%rsi,%r10,8), %r12
966384: incq %r10
966387: cmpq %r10, %r15
96638a: jne 0x966380 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3390>
96638c: jmp 0x9662b0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x32c0>
966391: testb %bpl, %bpl
966394: movl %r14d, %ecx
966397: movq 0x20(%rsp), %r8
96639c: movb $0x28, %r11b
96639f: cmoveq %r9, %rbx
9663a3: imulq %rbx, %rcx
9663a7: leaq 0x1(%r10,%rcx), %rax
9663ac: movzbl 0x9(%r10,%rcx), %ecx
9663b2: movq (%rax), %r14
9663b5: testb %cl, %cl
9663b7: jne 0x966284 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3294>
9663bd: addq %r14, %r12
9663c0: movl $0x1, %eax
9663c5: jmp 0x965f92 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa2>
9663ca: leaq (%rcx,%rcx,8), %rcx
9663ce: leaq 0x31(%r15,%rcx), %rax
9663d3: movzbl 0x39(%r15,%rcx), %ecx
9663d9: andb $0x1, %cl
9663dc: movq (%rax), %r14
9663df: testb %cl, %cl
9663e1: jne 0x966284 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x3294>
9663e7: jmp 0x9663bd <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x33cd>
9663e9: movq 0x20(%rsp), %r8
9663ee: xorl %eax, %eax
9663f0: movb $0x28, %r11b
9663f3: jmp 0x965f92 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x2fa2>
9663f8: xorl %r12d, %r12d
9663fb: movq %r12, 0x80(%rsp)
