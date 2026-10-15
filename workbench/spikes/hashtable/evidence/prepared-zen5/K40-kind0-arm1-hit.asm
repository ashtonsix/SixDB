Phase resolved from ['./build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:67', './build/hashtable-design-formats-avx2-1-0-1/./workbench/spikes/hashtable-design/formats.cpp:131']
Complete timed CFG, address order.
813583: movq 0x100(%rsp), %rdx
81358b: movq %rax, 0x28(%rsp)
813590: movq (%rdx), %rcx
813593: movq 0x8(%rdx), %r10
813597: movq %rcx, 0x20(%rsp)
81359c: cmpq %r10, %rcx
81359f: je 0x813993 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1cd3>
8135a5: movq 0x108(%rsp), %r8
8135ad: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
8135b7: vpxor %xmm0, %xmm0, %xmm0
8135bb: xorl %r12d, %r12d
8135be: movq 0x20(%r8), %r9
8135c2: movq 0x60(%r8), %rax
8135c6: movq 0x58(%r8), %r11
8135ca: movq %rax, 0x8(%rsp)
8135cf: xorl %eax, %eax
8135d1: movq %r9, 0x10(%rsp)
8135d6: jmp 0x8135ef <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x192f>
8135e0: movl 0x18(%rsp), %eax
8135e4: incl %eax
8135e6: cmpl $0x10, %eax
8135e9: je 0x813996 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1cd6>
8135ef: movq 0x20(%rsp), %r15
8135f4: movl %eax, 0x18(%rsp)
8135f8: jmp 0x81360f <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x194f>
813600: xorl %r14d, %r14d
813603: addq %r14, %r12
813606: addq $0x4, %r15
81360a: cmpq %r10, %r15
81360d: je 0x8135e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1920>
81360f: movl (%r15), %r13d
813612: movabsq $0xb86659fd93, %rax     # imm = 0xB86659FD93
81361c: movb $0x28, %cl
81361e: imulq %rax, %r13
813622: movq %r13, %rax
813625: xorq %r11, %rax
813628: bzhiq %rcx, %rax, %rbp
81362d: movq %rbp, %rcx
813630: shrq $0xa, %rcx
813634: movq %rcx, %rdx
813637: xorq 0x8(%rsp), %rdx
81363c: mulxq %rsi, %rdi, %rdx
813641: xorl %edi, %eax
813643: xorl %edx, %eax
813645: shll $0x7, %eax
813648: andl $0x1ff80, %eax          # imm = 0x1FF80
81364d: movzbl (%r9,%rax), %edi
813652: leaq (%r9,%rax), %r14
813656: testl %edi, %edi
813658: je 0x813750 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1a90>
81365e: movq 0x68(%r8), %rdx
813662: shrq $0x12, %rbp
813666: xorq %rbp, %rdx
813669: mulxq %rsi, %rdx, %rax
81366e: xorl %edx, %ecx
813670: xorl %eax, %ecx
813672: cmpl $0x10, %edi
813675: movl $0x10, %eax
81367a: vmovd %ecx, %xmm1
81367e: cmovbl %edi, %eax
813681: vpbroadcastb %xmm1, %xmm1
813686: vpcmpeqb 0x5(%r14), %xmm1, %xmm2
81368c: vpmovmskb %xmm2, %ecx
813690: bzhil %eax, %ecx, %edx
813695: je 0x8136d1 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1a11>
813697: nopw (%rax,%rax)
8136a0: tzcntq %rdx, %rcx
8136a5: leaq (%rcx,%rcx,2), %rax
8136a9: movzwl 0x23(%r14,%rax), %ebx
8136af: movzbl 0x25(%r14,%rax), %eax
8136b5: shll $0x10, %eax
8136b8: orl %ebx, %eax
8136ba: cmpl %eax, %ebp
8136bc: je 0x8138c0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c00>
8136c2: movq $-0x2, %rax
8136c9: rolq %cl, %rax
8136cc: andq %rax, %rdx
8136cf: jne 0x8136a0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x19e0>
8136d1: cmpb $0x11, %dil
8136d5: jb 0x813750 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1a90>
8136d7: vmovq 0x15(%r14), %xmm2
8136dd: addl $-0x10, %edi
8136e0: movl $0xe, %eax
8136e5: cmpl $0xe, %edi
8136e8: cmovael %eax, %edi
8136eb: vpinsrd $0x2, 0x1d(%r14), %xmm2, %xmm2
8136f2: vpinsrw $0x6, 0x21(%r14), %xmm2, %xmm2
8136f9: vpblendw $0x80, %xmm0, %xmm2, %xmm2 # xmm2 = xmm2[0,1,2,3,4,5,6],xmm0[7]
8136ff: vpcmpeqb %xmm1, %xmm2, %xmm1
813703: vpmovmskb %xmm1, %eax
813707: bzhil %edi, %eax, %edx
81370c: je 0x813750 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1a90>
81370e: nop 
813710: tzcntq %rdx, %rcx
813715: leaq (%rcx,%rcx,2), %rax
813719: movzwl 0x53(%r14,%rax), %edi
81371f: movzbl 0x55(%r14,%rax), %eax
813725: shll $0x10, %eax
813728: orl %edi, %eax
81372a: cmpl %eax, %ebp
81372c: je 0x8138c0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c00>
813732: movq $-0x2, %rax
813739: rolq %cl, %rax
81373c: andq %rax, %rdx
81373f: jne 0x813710 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1a50>
813741: nopw %cs:(%rax,%rax)
813750: cmpl $0x0, 0x1(%r14)
813755: je 0x813600 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1940>
81375b: movq 0x78(%r8), %rax
81375f: movq 0x80(%r8), %rdx
813766: movb $0x28, %cl
813768: movq 0x40(%r8), %rbx
81376c: xorq %r13, %rax
81376f: bzhiq %rcx, %rax, %rbp
813774: movq %rbp, %rcx
813777: shrq $0x6, %rcx
81377b: xorq %rcx, %rdx
81377e: mulxq %rsi, %rdi, %rdx
813783: xorl %edi, %eax
813785: xorl %edx, %eax
813787: andl $0x3f, %eax
81378a: shll $0x7, %eax
81378d: movzbl (%rbx,%rax), %edi
813791: leaq (%rbx,%rax), %r14
813795: testl %edi, %edi
813797: je 0x813890 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1bd0>
81379d: movq 0x88(%r8), %rdx
8137a4: shrq $0xe, %rbp
8137a8: xorq %rbp, %rdx
8137ab: mulxq %rsi, %rdx, %rax
8137b0: xorl %edx, %ecx
8137b2: xorl %eax, %ecx
8137b4: cmpl $0x10, %edi
8137b7: movl $0x10, %eax
8137bc: vmovd %ecx, %xmm1
8137c0: cmovbl %edi, %eax
8137c3: vpbroadcastb %xmm1, %xmm1
8137c8: vpcmpeqb 0x9(%r14), %xmm1, %xmm2
8137ce: vpmovmskb %xmm2, %ecx
8137d2: bzhil %eax, %ecx, %eax
8137d7: je 0x8137ff <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1b3f>
8137d9: nopl (%rax)
8137e0: tzcntq %rax, %rcx
8137e5: cmpl 0x20(%r14,%rcx,4), %ebp
8137ea: je 0x8138c0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c00>
8137f0: movq $-0x2, %rdx
8137f7: rolq %cl, %rdx
8137fa: andq %rdx, %rax
8137fd: jne 0x8137e0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1b20>
8137ff: cmpb $0x11, %dil
813803: jb 0x813890 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1bd0>
813809: addl $-0x10, %edi
81380c: movl $0x7, %eax
813811: vpinsrw $0x2, 0x1d(%r14), %xmm0, %xmm3
813818: cmpl $0x7, %edi
81381b: cmovael %eax, %edi
81381e: movl 0x19(%r14), %eax
813822: vmovd %eax, %xmm2
813826: vpand 0x90d922(%rip), %xmm2, %xmm2 # 0x1121150 <vsnprintf@GLIBC_2.2.5+0x1121150>
81382e: movl %eax, %ecx
813830: movl %eax, %edx
813832: shrl $0x8, %eax
813835: shrl $0x10, %edx
813838: shrl $0x18, %ecx
81383b: vpinsrb $0x1, %eax, %xmm2, %xmm2
813841: vpinsrb $0x2, %edx, %xmm2, %xmm2
813847: vpinsrb $0x3, %ecx, %xmm2, %xmm2
81384d: vpblendw $0x4, %xmm3, %xmm2, %xmm2 # xmm2 = xmm2[0,1],xmm3[2],xmm2[3,4,5,6,7]
813853: vpinsrb $0x6, 0x1f(%r14), %xmm2, %xmm2
81385a: vpcmpeqb %xmm1, %xmm2, %xmm1
81385e: vpmovmskb %xmm1, %eax
813862: bzhil %edi, %eax, %eax
813867: je 0x813890 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1bd0>
813869: nopl (%rax)
813870: tzcntq %rax, %rcx
813875: cmpl 0x60(%r14,%rcx,4), %ebp
81387a: je 0x8138c0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c00>
81387c: movq $-0x2, %rdx
813883: rolq %cl, %rdx
813886: andq %rdx, %rax
813889: jne 0x813870 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1bb0>
81388b: nopl (%rax,%rax)
813890: cmpl $0x0, 0x1(%r14)
813895: je 0x813600 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1940>
81389b: movl 0x5(%r14), %ebp
81389f: testl %ebp, %ebp
8138a1: je 0x813600 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1940>
8138a7: movq 0xa0(%r8), %rdx
8138ae: movq 0xa8(%r8), %rdi
8138b5: movb $0x28, %al
8138b7: bzhiq %rax, %r13, %rcx
8138bc: jmp 0x8138f8 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c38>
8138c0: movl $0x1, %r14d
8138c6: jmp 0x813603 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1943>
8138d0: movl 0x2(%r14,%r13), %ebp
8138d5: movq %r11, %r10
8138d8: movq %r9, %r11
8138db: movq 0x10(%rsp), %r9
8138e0: movl $0x0, %r14d
8138e6: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
8138f0: testl %ebp, %ebp
8138f2: je 0x813603 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1943>
8138f8: movl %ebp, %eax
8138fa: shrl $0x8, %eax
8138fd: movl $0x0, %esi
813902: movzbl %bpl, %r13d
813906: movq %r11, %r9
813909: movq %r10, %r11
81390c: decl %eax
81390e: leaq (%rax,%rax,4), %rax
813912: movl 0x20(%rdi,%rax,8), %ebx
813916: movq 0x8(%rdi,%rax,8), %r14
81391b: imulq $0x2c, %rbx, %rbx
81391f: movzwl 0x8(%rdx,%rbx), %eax
813924: cmpb $0x0, 0xa(%rdx,%rbx)
813929: cmovel %esi, %eax
81392c: imulq %rax, %r13
813930: movzwl 0x6(%r14,%r13), %eax
813936: testq %rax, %rax
813939: je 0x8138d0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c10>
81393b: leaq (%rax,%rax,4), %rbp
81393f: leaq 0xc(%r14,%r13), %rax
813944: xorl %ebx, %ebx
813946: nopw %cs:(%rax,%rax)
813950: movzbl (%rax,%rbx), %r10d
813955: movl -0x4(%rax,%rbx), %esi
813959: shlq $0x20, %r10
81395d: orq %rsi, %r10
813960: cmpq %rcx, %r10
813963: je 0x813973 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1cb3>
813965: addq $0x5, %rbx
813969: cmpq %rbx, %rbp
81396c: jne 0x813950 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c90>
81396e: jmp 0x8138d0 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1c10>
813973: movq %r11, %r10
813976: movq %r9, %r11
813979: movq 0x10(%rsp), %r9
81397e: movl $0x1, %r14d
813984: movabsq $0x79d5f9e0de1e8cf5, %rsi # imm = 0x79D5F9E0DE1E8CF5
81398e: jmp 0x813603 <void ht::format_trial<40u, (ht::layout_solver::container)0, 10u, 1u>(orbital::allocation::Pool&, unsigned int)+0x1943>
813993: xorl %r12d, %r12d
813996: movq %r12, 0x70(%rsp)
