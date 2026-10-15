0x8403a0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const>:
  8403a0:      	sub	sp, sp, #0x70
  8403a4:      	stp	x29, x30, [sp, #0x10]
  8403a8:      	stp	x28, x27, [sp, #0x20]
  8403ac:      	stp	x26, x25, [sp, #0x30]
  8403b0:      	stp	x24, x23, [sp, #0x40]
  8403b4:      	stp	x22, x21, [sp, #0x50]
  8403b8:      	stp	x20, x19, [sp, #0x60]
  8403bc:      	add	x29, sp, #0x10
  8403c0:      	mov	x25, #0x8cf5            // =36085
  8403c4:      	ldr	x24, [x0]
  8403c8:      	adrp	x8, 0x1181000 <ht::prepared_field<3u>+0x198>
  8403cc:      	add	x8, x8, #0x358
  8403d0:      	mov	w9, #0x28               // =40
  8403d4:      	adrp	x26, 0x1181000 <ht::prepared_field<3u>+0x198>
  8403d8:      	movk	x25, #0xde1e, lsl #16
  8403dc:      	mov	w10, #0x2c              // =44
  8403e0:      	mov	w11, #0x9               // =9
  8403e4:      	mov	x12, #-0x1              // =-1
  8403e8:      	mov	w13, #0xf               // =15
  8403ec:      	mov	w14, #0xa               // =10
  8403f0:      	movk	x25, #0xf9e0, lsl #32
  8403f4:      	mov	w15, #0x18              // =24
  8403f8:      	mov	w16, #0x78              // =120
  8403fc:      	mov	x19, x0
  840400:      	mov	w23, wzr
  840404:      	mov	x27, xzr
  840408:      	movk	x25, #0x79d5, lsl #48
  84040c:      	b	0x840448 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0xa8>
  840410:      	nop
  840414:      	nop
  840418:      	nop
  84041c:      	nop
  840420:      	mov	x0, xzr
  840424:      	mov	x18, xzr
  840428:      	ldr	x24, [x19]
  84042c:      	add	w23, w23, #0x1
  840430:      	mov	x27, x18
  840434:      	ldr	x17, [x24]
  840438:      	add	x17, x17, x0
  84043c:      	str	x17, [x24]
  840440:      	cmp	w23, #0x1c, lsl #12     // =0x1c000
  840444:      	b.eq	0x84083c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x49c>
  840448:      	ldr	x28, [x19, #0x8]
  84044c:      	ldp	x17, x1, [x28, #0x58]
  840450:      	eor	x18, x17, x27
  840454:      	ubfx	x0, x18, #10, #30
  840458:      	eor	x17, x0, x1
  84045c:      	mul	w1, w17, w25
  840460:      	umulh	x17, x17, x25
  840464:      	eor	w1, w18, w1
  840468:      	eor	w17, w1, w17
  84046c:      	ldr	x1, [x28, #0x20]
  840470:      	ubfiz	x17, x17, #7, #10
  840474:      	add	x17, x1, x17
  840478:      	ldrb	w1, [x17]
  84047c:      	cbz	w1, 0x840518 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x178>
  840480:      	ldr	x2, [x28, #0x68]
  840484:      	ubfx	x18, x18, #18, #22
  840488:      	ldur	d0, [x17, #0x5]
  84048c:      	movi	v1.2d, #0000000000000000
  840490:      	cmp	w1, #0xa
  840494:      	csel	w1, w1, w14, lo
  840498:      	lsl	w1, w1, #2
  84049c:      	lsl	x1, x12, x1
  8404a0:      	eor	x2, x2, x18
  8404a4:      	mul	w3, w2, w25
  8404a8:      	umulh	x2, x2, x25
  8404ac:      	eor	w0, w0, w3
  8404b0:      	eor	w0, w0, w2
  8404b4:      	ldurh	w2, [x17, #0xd]
  8404b8:      	mov	v1.s[0], w2
  8404bc:      	mov	v0.d[1], v1.d[0]
  8404c0:      	dup	v1.16b, w0
  8404c4:      	cmeq	v0.16b, v1.16b, v0.16b
  8404c8:      	shrn	v0.8b, v0.8h, #0x4
  8404cc:      	fmov	x0, d0
  8404d0:      	bics	x0, x0, x1
  8404d4:      	b.eq	0x840518 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x178>
  8404d8:      	nop
  8404dc:      	nop
  8404e0:      	rbit	x1, x0
  8404e4:      	clz	x1, x1
  8404e8:      	lsr	w2, w1, #2
  8404ec:      	madd	w3, w2, w15, w16
  8404f0:      	add	x3, x17, x3, lsr #3
  8404f4:      	ldrh	w4, [x3]
  8404f8:      	ldrb	w3, [x3, #0x2]
  8404fc:      	orr	x3, x4, x3, lsl #16
  840500:      	cmp	x3, x18
  840504:      	b.eq	0x840728 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x388>
  840508:      	and	x1, x1, #0x3c
  84050c:      	lsl	x1, x13, x1
  840510:      	bics	x0, x0, x1
  840514:      	b.ne	0x8404e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x140>
  840518:      	ldur	w17, [x17, #0x1]
  84051c:      	cbz	w17, 0x840420 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  840520:      	ldp	x17, x1, [x28, #0x78]
  840524:      	eor	x18, x17, x27
  840528:      	ubfx	x0, x18, #6, #34
  84052c:      	eor	x17, x0, x1
  840530:      	mul	w1, w17, w25
  840534:      	umulh	x17, x17, x25
  840538:      	eor	w1, w18, w1
  84053c:      	eor	w17, w1, w17
  840540:      	ldr	x1, [x28, #0x40]
  840544:      	ubfiz	x17, x17, #7, #6
  840548:      	add	x17, x1, x17
  84054c:      	ldrb	w1, [x17]
  840550:      	cbz	w1, 0x8405e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x240>
  840554:      	ldr	x2, [x28, #0x88]
  840558:      	ubfx	x18, x18, #14, #26
  84055c:      	ldur	d0, [x17, #0x9]
  840560:      	movi	v1.2d, #0000000000000000
  840564:      	cmp	w1, #0x9
  840568:      	csel	w1, w1, w11, lo
  84056c:      	lsl	w1, w1, #2
  840570:      	lsl	x1, x12, x1
  840574:      	eor	x2, x2, x18
  840578:      	mul	w3, w2, w25
  84057c:      	umulh	x2, x2, x25
  840580:      	eor	w0, w0, w3
  840584:      	eor	w0, w0, w2
  840588:      	ldrb	w2, [x17, #0x11]
  84058c:      	mov	v1.s[0], w2
  840590:      	mov	v0.d[1], v1.d[0]
  840594:      	dup	v1.16b, w0
  840598:      	cmeq	v0.16b, v1.16b, v0.16b
  84059c:      	shrn	v0.8b, v0.8h, #0x4
  8405a0:      	fmov	x0, d0
  8405a4:      	bics	x0, x0, x1
  8405a8:      	b.eq	0x8405e0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x240>
  8405ac:      	rbit	x1, x0
  8405b0:      	clz	x1, x1
  8405b4:      	lsr	w2, w1, #2
  8405b8:      	lsl	w3, w2, #5
  8405bc:      	add	w3, w3, #0x90
  8405c0:      	lsr	x3, x3, #3
  8405c4:      	ldr	w3, [x17, x3]
  8405c8:      	cmp	x18, x3
  8405cc:      	b.eq	0x84077c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3dc>
  8405d0:      	and	x1, x1, #0x3c
  8405d4:      	lsl	x1, x13, x1
  8405d8:      	bics	x0, x0, x1
  8405dc:      	b.ne	0x8405ac <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x20c>
  8405e0:      	ldur	w18, [x17, #0x1]
  8405e4:      	cbz	w18, 0x840420 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  8405e8:      	ldur	w21, [x17, #0x5]
  8405ec:      	cbnz	w21, 0x840610 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x270>
  8405f0:      	b	0x840420 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  8405f4:      	nop
  8405f8:      	nop
  8405fc:      	nop
  840600:      	ldur	w21, [x17, #0x2]
  840604:      	mov	x0, xzr
  840608:      	mov	x18, xzr
  84060c:      	cbz	w21, 0x840428 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x88>
  840610:      	adrp	x17, 0x1181000 <ht::prepared_field<3u>+0x198>
  840614:      	add	x17, x17, #0x370
  840618:      	ldr	x20, [x28, #0xa0]
  84061c:      	ldarb	w17, [x17]
  840620:      	tbz	w17, #0x0, 0x840690 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2f0>
  840624:      	lsr	w17, w21, #8
  840628:      	ldr	x0, [x26, #0x358]
  84062c:      	sub	w22, w17, #0x1
  840630:      	umaddl	x17, w22, w9, x20
  840634:      	ldr	w18, [x17, #0x20]
  840638:      	ldr	x17, [x17, #0x8]
  84063c:      	umaddl	x18, w18, w10, x0
  840640:      	ldrb	w0, [x18, #0xa]
  840644:      	ldrh	w18, [x18, #0x8]
  840648:      	cmp	w0, #0x0
  84064c:      	and	w0, w21, #0xff
  840650:      	csel	w18, w18, wzr, ne
  840654:      	mul	w18, w18, w0
  840658:      	add	x17, x17, x18
  84065c:      	ldrh	w18, [x17, #0x6]
  840660:      	cbz	x18, 0x840600 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x260>
  840664:      	add	x0, x17, #0xc
  840668:      	mov	x20, #-0xd              // =-13
  84066c:      	ldur	w1, [x0, #-0x4]
  840670:      	ldrb	w2, [x0], #0xd
  840674:      	orr	x1, x1, x2, lsl #32
  840678:      	cmp	x1, x27
  84067c:      	b.eq	0x840734 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x394>
  840680:      	sub	x18, x18, #0x1
  840684:      	sub	x20, x20, #0xd
  840688:      	cbnz	x18, 0x84066c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2cc>
  84068c:      	b	0x840600 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x260>
  840690:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  840694:      	add	x0, x0, #0x370
  840698:      	mov	x22, x8
  84069c:      	bl	0x7130 <__cxa_guard_acquire@plt>
  8406a0:      	mov	w16, #0x78              // =120
  8406a4:      	mov	w15, #0x18              // =24
  8406a8:      	mov	w14, #0xa               // =10
  8406ac:      	mov	w13, #0xf               // =15
  8406b0:      	mov	x12, #-0x1              // =-1
  8406b4:      	mov	w11, #0x9               // =9
  8406b8:      	mov	w10, #0x2c              // =44
  8406bc:      	mov	w9, #0x28               // =40
  8406c0:      	mov	x8, x22
  8406c4:      	cbz	w0, 0x840624 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x284>
  8406c8:      	sub	x0, x29, #0x2
  8406cc:      	mov	w26, w23
  8406d0:      	bl	0x83b700 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  8406d4:      	adrp	x0, 0x10000 <ht::record_table<8u, (ht::layout_solver::container)0, 0u, 1u, 0u, 64u, 64u, (ht::record_format)1, false, (ht::record_access)0>::record_table(orbital::allocation::Pool&, unsigned long, unsigned long)+0x2d0>
  8406d8:      	add	x0, x0, #0x560
  8406dc:      	adrp	x2, 0x1180000 <_GLOBAL_OFFSET_TABLE_+0x158>
  8406e0:      	add	x2, x2, #0x448
  8406e4:      	mov	x1, x22
  8406e8:      	bl	0x6e10 <__cxa_atexit@plt>
  8406ec:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  8406f0:      	add	x0, x0, #0x370
  8406f4:      	bl	0x6c90 <__cxa_guard_release@plt>
  8406f8:      	mov	w23, w26
  8406fc:      	mov	w9, #0x28               // =40
  840700:      	adrp	x26, 0x1181000 <ht::prepared_field<3u>+0x198>
  840704:      	mov	w10, #0x2c              // =44
  840708:      	mov	w11, #0x9               // =9
  84070c:      	mov	x12, #-0x1              // =-1
  840710:      	mov	w13, #0xf               // =15
  840714:      	mov	w14, #0xa               // =10
  840718:      	mov	w15, #0x18              // =24
  84071c:      	mov	w16, #0x78              // =120
  840720:      	mov	x8, x22
  840724:      	b	0x840624 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x284>
  840728:      	lsl	w18, w2, #6
  84072c:      	add	w18, w18, #0x168
  840730:      	b	0x840784 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3e4>
  840734:      	adrp	x17, 0x1181000 <ht::prepared_field<3u>+0x198>
  840738:      	add	x17, x17, #0x370
  84073c:      	ldr	x27, [x28, #0xa0]
  840740:      	ldarb	w17, [x17]
  840744:      	tbz	w17, #0x0, 0x8407a4 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x404>
  840748:      	umaddl	x17, w22, w9, x27
  84074c:      	ldr	x0, [x26, #0x358]
  840750:      	ldr	w18, [x17, #0x20]
  840754:      	ldr	x17, [x17, #0x8]
  840758:      	umaddl	x18, w18, w10, x0
  84075c:      	ldrb	w0, [x18, #0xa]
  840760:      	ldrh	w18, [x18, #0x8]
  840764:      	cmp	w0, #0x0
  840768:      	and	w0, w21, #0xff
  84076c:      	csel	x18, x18, xzr, ne
  840770:      	umaddl	x17, w18, w0, x17
  840774:      	sub	x17, x17, x20
  840778:      	b	0x84078c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3ec>
  84077c:      	lsl	w18, w2, #6
  840780:      	add	w18, w18, #0x1b0
  840784:      	lsr	w18, w18, #3
  840788:      	add	x17, x17, x18
  84078c:      	ldr	x18, [x17]
  840790:      	ldr	x17, [x24]
  840794:      	mov	w0, #0x1                // =1
  840798:      	add	x17, x17, x18
  84079c:      	str	x17, [x24]
  8407a0:      	b	0x840428 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x88>
  8407a4:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  8407a8:      	add	x0, x0, #0x370
  8407ac:      	mov	x28, x8
  8407b0:      	bl	0x7130 <__cxa_guard_acquire@plt>
  8407b4:      	mov	w16, #0x78              // =120
  8407b8:      	mov	w15, #0x18              // =24
  8407bc:      	mov	w14, #0xa               // =10
  8407c0:      	mov	w13, #0xf               // =15
  8407c4:      	mov	x12, #-0x1              // =-1
  8407c8:      	mov	w11, #0x9               // =9
  8407cc:      	mov	w10, #0x2c              // =44
  8407d0:      	mov	w9, #0x28               // =40
  8407d4:      	mov	x8, x28
  8407d8:      	cbz	w0, 0x840748 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3a8>
  8407dc:      	sub	x0, x29, #0x1
  8407e0:      	mov	w26, w23
  8407e4:      	bl	0x83b700 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  8407e8:      	adrp	x0, 0x10000 <ht::record_table<8u, (ht::layout_solver::container)0, 0u, 1u, 0u, 64u, 64u, (ht::record_format)1, false, (ht::record_access)0>::record_table(orbital::allocation::Pool&, unsigned long, unsigned long)+0x2d0>
  8407ec:      	add	x0, x0, #0x560
  8407f0:      	adrp	x2, 0x1180000 <_GLOBAL_OFFSET_TABLE_+0x158>
  8407f4:      	add	x2, x2, #0x448
  8407f8:      	mov	x1, x28
  8407fc:      	bl	0x6e10 <__cxa_atexit@plt>
  840800:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  840804:      	add	x0, x0, #0x370
  840808:      	bl	0x6c90 <__cxa_guard_release@plt>
  84080c:      	mov	x8, x28
  840810:      	mov	w23, w26
  840814:      	mov	w9, #0x28               // =40
  840818:      	adrp	x26, 0x1181000 <ht::prepared_field<3u>+0x198>
  84081c:      	mov	w10, #0x2c              // =44
  840820:      	mov	w11, #0x9               // =9
  840824:      	mov	x12, #-0x1              // =-1
  840828:      	mov	w13, #0xf               // =15
  84082c:      	mov	w14, #0xa               // =10
  840830:      	mov	w15, #0x18              // =24
  840834:      	mov	w16, #0x78              // =120
  840838:      	b	0x840748 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3a8>
  84083c:      	ldp	x20, x19, [sp, #0x60]
  840840:      	ldp	x22, x21, [sp, #0x50]
  840844:      	ldp	x24, x23, [sp, #0x40]
  840848:      	ldp	x26, x25, [sp, #0x30]
  84084c:      	ldp	x28, x27, [sp, #0x20]
  840850:      	ldp	x29, x30, [sp, #0x10]
  840854:      	add	sp, sp, #0x70
  840858:      	ret
  84085c:      	mov	x19, x0
  840860:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  840864:      	add	x0, x0, #0x370
  840868:      	bl	0x6c70 <__cxa_guard_abort@plt>
  84086c:      	mov	x0, x19
  840870:      	bl	0x7110 <_Unwind_Resume@plt>
  840874:      	mov	x19, x0
  840878:      	adrp	x0, 0x1181000 <ht::prepared_field<3u>+0x198>
  84087c:      	add	x0, x0, #0x370
  840880:      	bl	0x6c70 <__cxa_guard_abort@plt>
  840884:      	mov	x0, x19
  840888:      	bl	0x7110 <_Unwind_Resume@plt>
  84088c:      	nop
  840890:      	nop
  840894:      	nop
  840898:      	nop
  84089c:      	nop

