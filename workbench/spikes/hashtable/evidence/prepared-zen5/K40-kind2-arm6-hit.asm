Phase resolved from ['./build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:67', './build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:131']
Complete timed CFG, address order.
9f00f3: movq %rax, 0x88(%rsp)
9f00fb: movq 0x1c8(%rsp), %rax
9f0103: movq 0x1c0(%rsp), %r8
9f010b: movq 0x48(%rsp), %rdi
9f0110: movl $0xffffff, %r11d        # imm = 0xFFFFFF
9f0116: xorl %ebp, %ebp
9f0118: xorl %r14d, %r14d
9f011b: movq %rax, (%rsp)
9f011f: movq 0x188(%rsp), %rax
9f0127: movq %r8, 0x30(%rsp)
9f012c: movq %rax, 0x8(%rsp)
9f0131: movq 0x1a8(%rsp), %rax
9f0139: movq %rax, 0x28(%rsp)
9f013e: movq 0x1d0(%rsp), %rax
9f0146: movq %rax, 0x10(%rsp)
9f014b: movq 0x1e0(%rsp), %rax
9f0153: movq %rax, 0x20(%rsp)
9f0158: movq 0x1e8(%rsp), %rax
9f0160: movq %rax, 0x40(%rsp)
9f0165: movq 0x1f0(%rsp), %rax
9f016d: movq %rax, 0x50(%rsp)
9f0172: movq 0x210(%rsp), %rax
9f017a: movq %rax, 0x18(%rsp)
9f017f: movq 0x208(%rsp), %rax
9f0187: movq %rax, 0x38(%rsp)
9f018c: movq 0x280(%rsp), %rax
9f0194: movq %rax, 0x58(%rsp)
9f0199: xorl %eax, %eax
9f019b: jmp 0x9f01af <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d2f>
9f01a0: movl 0x68(%rsp), %eax
9f01a4: incl %eax
9f01a6: cmpl $0x10, %eax
9f01a9: je 0x9f05bb <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x313b>
9f01af: xorl %r10d, %r10d
9f01b2: movl %eax, 0x68(%rsp)
9f01b6: jmp 0x9f01d8 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d58>
9f01c0: xorl %ecx, %ecx
9f01c2: movq %r14, %rax
9f01c5: movq %rcx, %r14
9f01c8: addq %rax, %r14
9f01cb: addq $0x4, %r10
9f01cf: cmpq $0x7000, %r10           # imm = 0x7000
9f01d6: je 0x9f01a0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d20>
9f01d8: movl (%rdi,%r10), %r9d
9f01dc: movabsq $0xb86659fd93, %rax     # imm = 0xB86659FD93
9f01e6: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
9f01f0: imulq %rax, %r9
9f01f4: movb $0x28, %al
9f01f6: movq %r9, %rcx
9f01f9: xorq %r8, %rcx
9f01fc: bzhiq %rax, %rcx, %r15
9f0201: movq %r15, %rax
9f0204: shrq $0xa, %rax
9f0208: movq %rax, %rdx
9f020b: xorq (%rsp), %rdx
9f020f: mulxq %rsi, %rsi, %rdx
9f0214: xorl %esi, %ecx
9f0216: movq 0x8(%rsp), %rsi
9f021b: xorl %edx, %ecx
9f021d: shll $0x7, %ecx
9f0220: andl $0x1ff80, %ecx          # imm = 0x1FF80
9f0226: leaq (%rsi,%rcx), %r12
9f022a: movzbl (%rsi,%rcx), %ecx
9f022e: testb %cl, %cl
9f0230: je 0x9f02a9 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2e29>
9f0232: movq 0x10(%rsp), %rdx
9f0237: shrq $0x12, %r15
9f023b: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
9f0245: xorq %r15, %rdx
9f0248: mulxq %rsi, %rsi, %rdx
9f024d: xorl %esi, %eax
9f024f: xorl %edx, %eax
9f0251: movzbl %cl, %edx
9f0254: cmpb $0x9, %cl
9f0257: movl $0x9, %ecx
9f025c: vmovd %eax, %xmm0
9f0260: cmovael %ecx, %edx
9f0263: vpbroadcastb %xmm0, %xmm0
9f0268: vpcmpeqb 0x5(%r12), %xmm0, %xmm0
9f026f: vpmovmskb %xmm0, %eax
9f0273: bzhil %edx, %eax, %eax
9f0278: je 0x9f02a9 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2e29>
9f027a: nopw (%rax,%rax)
9f0280: tzcntq %rax, %rcx
9f0285: leaq (%rcx,%rcx,2), %rdx
9f0289: movl 0xe(%r12,%rdx), %edx
9f028e: andl %r11d, %edx
9f0291: cmpl %edx, %r15d
9f0294: je 0x9f0428 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2fa8>
9f029a: movq $-0x2, %rdx
9f02a1: rolq %cl, %rdx
9f02a4: andq %rdx, %rax
9f02a7: jne 0x9f0280 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2e00>
9f02a9: cmpl $0x0, 0x1(%r12)
9f02af: je 0x9f01c0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d40>
9f02b5: movq 0x20(%rsp), %rcx
9f02ba: movb $0x28, %al
9f02bc: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
9f02c6: xorq %r9, %rcx
9f02c9: bzhiq %rax, %rcx, %r15
9f02ce: movq %r15, %rax
9f02d1: shrq $0x6, %rax
9f02d5: movq %rax, %rdx
9f02d8: xorq 0x40(%rsp), %rdx
9f02dd: mulxq %rsi, %rsi, %rdx
9f02e2: xorl %esi, %ecx
9f02e4: movq 0x28(%rsp), %rsi
9f02e9: xorl %edx, %ecx
9f02eb: andl $0x3f, %ecx
9f02ee: shll $0x7, %ecx
9f02f1: leaq (%rsi,%rcx), %r12
9f02f5: movzbl (%rsi,%rcx), %ecx
9f02f9: testb %cl, %cl
9f02fb: je 0x9f036f <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2eef>
9f02fd: movq 0x50(%rsp), %rdx
9f0302: shrq $0xe, %r15
9f0306: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
9f0310: xorq %r15, %rdx
9f0313: mulxq %rsi, %rsi, %rdx
9f0318: xorl %esi, %eax
9f031a: xorl %edx, %eax
9f031c: movzbl %cl, %edx
9f031f: cmpb $0x8, %cl
9f0322: movl $0x8, %ecx
9f0327: vmovd %eax, %xmm0
9f032b: cmovael %ecx, %edx
9f032e: vpbroadcastb %xmm0, %xmm0
9f0333: vpcmpeqb 0x9(%r12), %xmm0, %xmm0
9f033a: vpmovmskb %xmm0, %eax
9f033e: bzhil %edx, %eax, %eax
9f0343: je 0x9f036f <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2eef>
9f0345: nopw %cs:(%rax,%rax)
9f0350: tzcntq %rax, %rcx
9f0355: cmpl 0x11(%r12,%rcx,4), %r15d
9f035a: je 0x9f058b <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x310b>
9f0360: movq $-0x2, %rdx
9f0367: rolq %cl, %rdx
9f036a: andq %rdx, %rax
9f036d: jne 0x9f0350 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2ed0>
9f036f: cmpl $0x0, 0x1(%r12)
9f0375: je 0x9f01c0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d40>
9f037b: movl 0x5(%r12), %eax
9f0380: testl %eax, %eax
9f0382: je 0x9f01c0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d40>
9f0388: movb $0x28, %cl
9f038a: bzhiq %rcx, %r9, %rcx
9f038f: jmp 0x9f03ad <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2f2d>
9f03a0: movl 0x2(%r9,%rsi), %eax
9f03a5: testl %eax, %eax
9f03a7: je 0x9f05aa <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x312a>
9f03ad: movq 0x18(%rsp), %rdi
9f03b2: movl %eax, %edx
9f03b4: shrl $0x8, %edx
9f03b7: movl $0x0, %r8d
9f03bd: movzbl %al, %eax
9f03c0: decl %edx
9f03c2: leaq (%rdx,%rdx,4), %rdx
9f03c6: movl 0x20(%rdi,%rdx,8), %esi
9f03ca: movq 0x8(%rdi,%rdx,8), %r9
9f03cf: movq 0x38(%rsp), %rdx
9f03d4: imulq $0x2c, %rsi, %rsi
9f03d8: movzbl 0xa(%rdx,%rsi), %r15d
9f03de: movzwl 0x8(%rdx,%rsi), %edx
9f03e3: movq %rax, %rsi
9f03e6: testb %r15b, %r15b
9f03e9: cmovnel %edx, %r8d
9f03ed: imulq %r8, %rsi
9f03f1: movzwl 0x6(%r9,%rsi), %r8d
9f03f7: testq %r8, %r8
9f03fa: je 0x9f03a0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2f20>
9f03fc: leaq 0xc(%r9), %r12
9f0400: movzbl (%r12,%rsi), %edi
9f0405: movl -0x4(%r12,%rsi), %r13d
9f040a: shlq $0x20, %rdi
9f040e: orq %r13, %rdi
9f0411: cmpq %rcx, %rdi
9f0414: je 0x9f0551 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x30d1>
9f041a: addq $0xe, %r12
9f041e: decq %r8
9f0421: jne 0x9f0400 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2f80>
9f0423: jmp 0x9f03a0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2f20>
9f0428: leaq (%rcx,%rcx,8), %rcx
9f042c: leaq 0x29(%r12,%rcx), %rax
9f0431: movzbl 0x31(%r12,%rcx), %ecx
9f0437: andb $0x1, %cl
9f043a: movq (%rax), %rax
9f043d: testb %cl, %cl
9f043f: je 0x9f057e <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x30fe>
9f0445: movl $0x1, %ecx
9f044a: testl %eax, %eax
9f044c: je 0x9f01c2 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d42>
9f0452: movq 0x58(%rsp), %rsi
9f0457: movq 0x8(%rsi), %rdx
9f045b: movq 0x10(%rsi), %r9
9f045f: jmp 0x9f047d <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2ffd>
9f0470: movl 0x2(%r15,%r12), %eax
9f0475: testl %eax, %eax
9f0477: je 0x9f05ac <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x312c>
9f047d: movl %eax, %esi
9f047f: shrl $0x8, %esi
9f0482: movzbl %al, %r12d
9f0486: decl %esi
9f0488: leaq (%rsi,%rsi,4), %rsi
9f048c: movl 0x20(%r9,%rsi,8), %edi
9f0491: movq 0x8(%r9,%rsi,8), %r15
9f0496: imulq $0x2c, %rdi, %rdi
9f049a: movzwl 0x8(%rdx,%rdi), %esi
9f049f: cmpb $0x0, 0xa(%rdx,%rdi)
9f04a4: cmovel %ebp, %esi
9f04a7: imulq %rsi, %r12
9f04ab: movzwl 0x6(%r15,%r12), %r8d
9f04b1: testq %r8, %r8
9f04b4: je 0x9f0470 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2ff0>
9f04b6: cmpw $0x8, %r8w
9f04bb: jae 0x9f04d0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x3050>
9f04bd: xorl %eax, %eax
9f04bf: jmp 0x9f0520 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x30a0>
9f04d0: leaq 0x2(%r15,%r12), %rsi
9f04d5: movl %r8d, %r13d
9f04d8: andl $-0x8, %r13d
9f04dc: xorl %eax, %eax
9f04de: addq $0x6, %rsi
9f04e2: addq $0x38, %rsi
9f04e6: nopw %cs:(%rax,%rax)
9f04f0: addq -0x38(%rsi,%rax,8), %r14
9f04f5: addq -0x30(%rsi,%rax,8), %r14
9f04fa: addq -0x28(%rsi,%rax,8), %r14
9f04ff: addq -0x20(%rsi,%rax,8), %r14
9f0504: addq -0x18(%rsi,%rax,8), %r14
9f0509: addq -0x10(%rsi,%rax,8), %r14
9f050e: addq -0x8(%rsi,%rax,8), %r14
9f0513: addq (%rsi,%rax,8), %r14
9f0517: addq $0x8, %rax
9f051b: cmpq %rax, %r13
9f051e: jne 0x9f04f0 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x3070>
9f0520: andl $0x7, %r8d
9f0524: je 0x9f0470 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2ff0>
9f052a: leaq (%r12,%rax,8), %rax
9f052e: xorl %esi, %esi
9f0530: leaq 0x8(%r15,%rax), %rax
9f0535: nopw %cs:(%rax,%rax)
9f0540: addq (%rax,%rsi,8), %r14
9f0544: incq %rsi
9f0547: cmpq %rsi, %r8
9f054a: jne 0x9f0540 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x30c0>
9f054c: jmp 0x9f0470 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2ff0>
9f0551: testb %r15b, %r15b
9f0554: movl %eax, %ecx
9f0556: movq 0x48(%rsp), %rdi
9f055b: movq 0x30(%rsp), %r8
9f0560: cmoveq %rbp, %rdx
9f0564: imulq %rdx, %rcx
9f0568: leaq 0x1(%r12,%rcx), %rax
9f056d: movzbl 0x9(%r12,%rcx), %ecx
9f0573: movq (%rax), %rax
9f0576: testb %cl, %cl
9f0578: jne 0x9f0445 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2fc5>
9f057e: addq %rax, %r14
9f0581: movl $0x1, %ecx
9f0586: jmp 0x9f01c2 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d42>
9f058b: leaq (%rcx,%rcx,8), %rcx
9f058f: leaq 0x31(%r12,%rcx), %rax
9f0594: movzbl 0x39(%r12,%rcx), %ecx
9f059a: andb $0x1, %cl
9f059d: movq (%rax), %rax
9f05a0: testb %cl, %cl
9f05a2: jne 0x9f0445 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2fc5>
9f05a8: jmp 0x9f057e <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x30fe>
9f05aa: xorl %ecx, %ecx
9f05ac: movq 0x48(%rsp), %rdi
9f05b1: movq 0x30(%rsp), %r8
9f05b6: jmp 0x9f01c2 <void ht::format_trial<40u, (ht::layout_solver::container)2, 10u, 6u>(orbital::allocation::Pool&, unsigned int)+0x2d42>
