Phase resolved from ['./build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:67', './build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:131']
Complete timed CFG, address order.
800334: movq 0x170(%rsp), %rcx
80033c: movq %rax, 0xb0(%rsp)
800344: movq 0x160(%rsp), %rax
80034c: movq 0x168(%rsp), %rsi
800354: movq 0x128(%rsp), %rdi
80035c: movq 0x148(%rsp), %r8
800364: movb $0x28, %r14b
800367: xorl %r15d, %r15d
80036a: movq %rcx, (%rsp)
80036e: movq 0x180(%rsp), %rcx
800376: movq %rcx, 0x18(%rsp)
80037b: movq 0x188(%rsp), %rcx
800383: movq %rcx, 0x10(%rsp)
800388: movq 0x190(%rsp), %rcx
800390: movq %rcx, 0x20(%rsp)
800395: movq 0x1b0(%rsp), %rcx
80039d: movq %rcx, 0x8(%rsp)
8003a2: movq 0x1a8(%rsp), %rcx
8003aa: movq %rcx, 0x28(%rsp)
8003af: xorl %ecx, %ecx
8003b1: jmp 0x8003cf <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x187f>
8003c0: movl 0x38(%rsp), %ecx
8003c4: incl %ecx
8003c6: cmpl $0x10, %ecx
8003c9: je 0x800739 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1be9>
8003cf: xorl %r11d, %r11d
8003d2: movl %ecx, 0x38(%rsp)
8003d6: jmp 0x8003f5 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x18a5>
8003e0: movl $0x1, %edx
8003e5: addq %rdx, %r15
8003e8: addq $0x4, %r11
8003ec: cmpq $0xe000, %r11           # imm = 0xE000
8003f3: je 0x8003c0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1870>
8003f5: movq 0x30(%rsp), %rcx
8003fa: movabsq $0x79d5f9e0de1e8cf5, %rbx # imm = 0x79D5F9E0DE1E8CF5
800404: movl (%rcx,%r11), %r10d
800408: movabsq $0xb86659fd93, %rcx     # imm = 0xB86659FD93
800412: imulq %rcx, %r10
800416: movq %r10, %rbp
800419: xorq %rax, %rbp
80041c: bzhiq %r14, %rbp, %r9
800421: movq %r9, %rcx
800424: shrq $0xa, %rcx
800428: movq %rcx, %rdx
80042b: xorq %rsi, %rdx
80042e: mulxq %rbx, %r13, %rdx
800433: xorl %r13d, %ebp
800436: xorl %edx, %ebp
800438: shll $0x7, %ebp
80043b: andl $0x1ff80, %ebp          # imm = 0x1FF80
800441: leaq (%rdi,%rbp), %r13
800445: movzbl (%rdi,%rbp), %ebp
800449: testl %ebp, %ebp
80044b: je 0x800550 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1a00>
800451: movq (%rsp), %rdx
800455: shrq $0x12, %r9
800459: xorq %r9, %rdx
80045c: mulxq %rbx, %r12, %rdx
800461: xorl %r12d, %ecx
800464: xorl %edx, %ecx
800466: cmpl $0x10, %ebp
800469: movl $0x10, %edx
80046e: vmovd %ecx, %xmm0
800472: cmovbl %ebp, %edx
800475: vpbroadcastb %xmm0, %xmm0
80047a: vpcmpeqb 0x5(%r13), %xmm0, %xmm1
800480: vpmovmskb %xmm1, %ecx
800484: bzhil %edx, %ecx, %edx
800489: je 0x8004c4 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1974>
80048b: nopl (%rax,%rax)
800490: tzcntq %rdx, %rcx
800495: leaq (%rcx,%rcx,2), %r12
800499: movzwl 0x23(%r13,%r12), %ebx
80049f: movzbl 0x25(%r13,%r12), %r12d
8004a5: shll $0x10, %r12d
8004a9: orl %ebx, %r12d
8004ac: cmpl %r9d, %r12d
8004af: je 0x8003e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1890>
8004b5: movq $-0x2, %rbx
8004bc: rolq %cl, %rbx
8004bf: andq %rbx, %rdx
8004c2: jne 0x800490 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1940>
8004c4: cmpb $0x11, %bpl
8004c8: jb 0x800550 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1a00>
8004ce: addl $-0x10, %ebp
8004d1: movl $0xe, %ecx
8004d6: movl 0x1f(%r13), %edx
8004da: vmovq 0x15(%r13), %xmm2
8004e0: cmpl $0xe, %ebp
8004e3: cmovael %ecx, %ebp
8004e6: movl 0x1d(%r13), %ecx
8004ea: shlq $0x10, %rdx
8004ee: orq %rcx, %rdx
8004f1: vmovq %rdx, %xmm1
8004f6: vpunpcklqdq %xmm1, %xmm2, %xmm1 # xmm1 = xmm2[0],xmm1[0]
8004fa: vpcmpeqb %xmm1, %xmm0, %xmm0
8004fe: vpmovmskb %xmm0, %ecx
800502: bzhil %ebp, %ecx, %edx
800507: je 0x800550 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1a00>
800509: nopl (%rax)
800510: tzcntq %rdx, %rcx
800515: leaq (%rcx,%rcx,2), %rbx
800519: movzwl 0x53(%r13,%rbx), %ebp
80051f: movzbl 0x55(%r13,%rbx), %ebx
800525: shll $0x10, %ebx
800528: orl %ebp, %ebx
80052a: cmpl %r9d, %ebx
80052d: je 0x8003e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1890>
800533: movq $-0x2, %rbx
80053a: rolq %cl, %rbx
80053d: andq %rbx, %rdx
800540: jne 0x800510 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x19c0>
800542: nopw %cs:(%rax,%rax)
800550: cmpl $0x0, 0x1(%r13)
800555: je 0x800690 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b40>
80055b: movq 0x18(%rsp), %rbx
800560: movabsq $0x79d5f9e0de1e8cf5, %r12 # imm = 0x79D5F9E0DE1E8CF5
80056a: xorq %r10, %rbx
80056d: bzhiq %r14, %rbx, %r9
800572: movq %r9, %rcx
800575: shrq $0x6, %rcx
800579: movq %rcx, %rdx
80057c: xorq 0x10(%rsp), %rdx
800581: mulxq %r12, %r12, %rdx
800586: xorl %r12d, %ebx
800589: xorl %edx, %ebx
80058b: andl $0x3f, %ebx
80058e: shll $0x7, %ebx
800591: movzbl (%r8,%rbx), %ebp
800596: leaq (%r8,%rbx), %r13
80059a: testl %ebp, %ebp
80059c: je 0x800670 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b20>
8005a2: movq 0x20(%rsp), %rdx
8005a7: shrq $0xe, %r9
8005ab: movabsq $0x79d5f9e0de1e8cf5, %rbx # imm = 0x79D5F9E0DE1E8CF5
8005b5: xorq %r9, %rdx
8005b8: mulxq %rbx, %rbx, %rdx
8005bd: xorl %ebx, %ecx
8005bf: xorl %edx, %ecx
8005c1: cmpl $0x10, %ebp
8005c4: movl $0x10, %edx
8005c9: vmovd %ecx, %xmm0
8005cd: cmovbl %ebp, %edx
8005d0: vpbroadcastb %xmm0, %xmm0
8005d5: vpcmpeqb 0x9(%r13), %xmm0, %xmm1
8005db: vpmovmskb %xmm1, %ecx
8005df: bzhil %edx, %ecx, %edx
8005e4: je 0x80060f <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1abf>
8005e6: nopw %cs:(%rax,%rax)
8005f0: tzcntq %rdx, %rcx
8005f5: cmpl 0x20(%r13,%rcx,4), %r9d
8005fa: je 0x8003e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1890>
800600: movq $-0x2, %rbx
800607: rolq %cl, %rbx
80060a: andq %rbx, %rdx
80060d: jne 0x8005f0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1aa0>
80060f: cmpb $0x11, %bpl
800613: jb 0x800670 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b20>
800615: addl $-0x10, %ebp
800618: movl $0x7, %ecx
80061d: movl 0x1c(%r13), %edx
800621: cmpl $0x7, %ebp
800624: cmovael %ecx, %ebp
800627: movl 0x19(%r13), %ecx
80062b: shlq $0x18, %rdx
80062f: orq %rcx, %rdx
800632: vmovq %rdx, %xmm1
800637: vpcmpeqb %xmm1, %xmm0, %xmm0
80063b: vpmovmskb %xmm0, %ecx
80063f: bzhil %ebp, %ecx, %edx
800644: je 0x800670 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b20>
800646: nopw %cs:(%rax,%rax)
800650: tzcntq %rdx, %rcx
800655: cmpl 0x60(%r13,%rcx,4), %r9d
80065a: je 0x8003e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1890>
800660: movq $-0x2, %rbx
800667: rolq %cl, %rbx
80066a: andq %rbx, %rdx
80066d: jne 0x800650 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b00>
80066f: nop 
800670: cmpl $0x0, 0x1(%r13)
800675: je 0x800690 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b40>
800677: movl 0x5(%r13), %r9d
80067b: testl %r9d, %r9d
80067e: je 0x800690 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b40>
800680: bzhiq %r14, %r10, %rcx
800685: jmp 0x8006b3 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b63>
800690: xorl %edx, %edx
800692: jmp 0x8003e5 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1895>
8006a0: movl 0x2(%rdx,%r9), %r9d
8006a5: movl $0x0, %edx
8006aa: testl %r9d, %r9d
8006ad: je 0x8003e5 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1895>
8006b3: movq 0x8(%rsp), %rbx
8006b8: movl %r9d, %edx
8006bb: shrl $0x8, %edx
8006be: movq 0x28(%rsp), %r12
8006c3: movzbl %r9b, %r9d
8006c7: decl %edx
8006c9: leaq (%rdx,%rdx,4), %rdx
8006cd: movl 0x20(%rbx,%rdx,8), %r10d
8006d2: movq 0x8(%rbx,%rdx,8), %rdx
8006d7: imulq $0x2c, %r10, %r10
8006db: movzwl 0x8(%r12,%r10), %ebx
8006e1: cmpb $0x0, 0xa(%r12,%r10)
8006e7: movl $0x0, %r10d
8006ed: cmovel %r10d, %ebx
8006f1: imulq %rbx, %r9
8006f5: movzwl 0x6(%rdx,%r9), %r10d
8006fb: testq %r10, %r10
8006fe: je 0x8006a0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b50>
800700: leaq (%r10,%r10,4), %r10
800704: leaq 0xc(%rdx,%r9), %r13
800709: xorl %ebp, %ebp
80070b: nopl (%rax,%rax)
800710: movzbl (%r13,%rbp), %r12d
800716: movl -0x4(%r13,%rbp), %ebx
80071b: shlq $0x20, %r12
80071f: orq %rbx, %r12
800722: cmpq %rcx, %r12
800725: je 0x8003e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1890>
80072b: addq $0x5, %rbp
80072f: cmpq %rbp, %r10
800732: jne 0x800710 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1bc0>
800734: jmp 0x8006a0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 0u>(orbital::allocation::Pool&, unsigned int)+0x1b50>
800739: movq %r15, 0x70(%rsp)
