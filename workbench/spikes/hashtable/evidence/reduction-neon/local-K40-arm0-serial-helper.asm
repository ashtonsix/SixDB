0x860e00 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const>:
  860e00:      	sub	sp, sp, #0x70
  860e04:      	stp	x29, x30, [sp, #0x10]
  860e08:      	stp	x28, x27, [sp, #0x20]
  860e0c:      	stp	x26, x25, [sp, #0x30]
  860e10:      	stp	x24, x23, [sp, #0x40]
  860e14:      	stp	x22, x21, [sp, #0x50]
  860e18:      	stp	x20, x19, [sp, #0x60]
  860e1c:      	add	x29, sp, #0x10
  860e20:      	mov	x25, #0x8cf5            // =36085
  860e24:      	adrp	x8, 0x11c1000 <ht::prepared_field<3u>+0x198>
  860e28:      	add	x8, x8, #0x358
  860e2c:      	mov	w9, #0x28               // =40
  860e30:      	adrp	x26, 0x11c1000 <ht::prepared_field<3u>+0x198>
  860e34:      	mov	w10, #0x2c              // =44
  860e38:      	movk	x25, #0xde1e, lsl #16
  860e3c:      	mov	w11, #0x9               // =9
  860e40:      	mov	x12, #-0x1              // =-1
  860e44:      	mov	w13, #0xf               // =15
  860e48:      	mov	w14, #0xa               // =10
  860e4c:      	mov	w15, #0x18              // =24
  860e50:      	movk	x25, #0xf9e0, lsl #32
  860e54:      	mov	w16, #0x78              // =120
  860e58:      	mov	x19, x0
  860e5c:      	mov	w24, wzr
  860e60:      	mov	x27, xzr
  860e64:      	mov	x20, xzr
  860e68:      	movk	x25, #0x79d5, lsl #48
  860e6c:      	b	0x860e94 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x94>
  860e70:      	nop
  860e74:      	nop
  860e78:      	nop
  860e7c:      	nop
  860e80:      	mov	x27, xzr
  860e84:      	add	x20, x20, xzr
  860e88:      	add	w24, w24, #0x1
  860e8c:      	cmp	w24, #0x1c, lsl #12     // =0x1c000
  860e90:      	b.eq	0x861284 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x484>
  860e94:      	ldr	x28, [x19]
  860e98:      	ldp	x17, x1, [x28, #0x58]
  860e9c:      	eor	x18, x17, x27
  860ea0:      	ubfx	x0, x18, #10, #30
  860ea4:      	eor	x17, x0, x1
  860ea8:      	mul	w1, w17, w25
  860eac:      	umulh	x17, x17, x25
  860eb0:      	eor	w1, w18, w1
  860eb4:      	eor	w17, w1, w17
  860eb8:      	ldr	x1, [x28, #0x20]
  860ebc:      	ubfiz	x17, x17, #7, #10
  860ec0:      	add	x17, x1, x17
  860ec4:      	ldrb	w1, [x17]
  860ec8:      	cbz	w1, 0x860f5c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x15c>
  860ecc:      	ldr	x2, [x28, #0x68]
  860ed0:      	ubfx	x18, x18, #18, #22
  860ed4:      	ldur	d0, [x17, #0x5]
  860ed8:      	movi	v1.2d, #0000000000000000
  860edc:      	cmp	w1, #0xa
  860ee0:      	csel	w1, w1, w14, lo
  860ee4:      	lsl	w1, w1, #2
  860ee8:      	lsl	x1, x12, x1
  860eec:      	eor	x2, x2, x18
  860ef0:      	mul	w3, w2, w25
  860ef4:      	umulh	x2, x2, x25
  860ef8:      	eor	w0, w0, w3
  860efc:      	eor	w0, w0, w2
  860f00:      	ldurh	w2, [x17, #0xd]
  860f04:      	mov	v1.s[0], w2
  860f08:      	mov	v0.d[1], v1.d[0]
  860f0c:      	dup	v1.16b, w0
  860f10:      	cmeq	v0.16b, v1.16b, v0.16b
  860f14:      	shrn	v0.8b, v0.8h, #0x4
  860f18:      	fmov	x0, d0
  860f1c:      	bics	x0, x0, x1
  860f20:      	b.eq	0x860f5c <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x15c>
  860f24:      	rbit	x1, x0
  860f28:      	clz	x1, x1
  860f2c:      	lsr	w2, w1, #2
  860f30:      	madd	w3, w2, w15, w16
  860f34:      	add	x3, x17, x3, lsr #3
  860f38:      	ldrh	w4, [x3]
  860f3c:      	ldrb	w3, [x3, #0x2]
  860f40:      	orr	x3, x4, x3, lsl #16
  860f44:      	cmp	x3, x18
  860f48:      	b.eq	0x861168 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x368>
  860f4c:      	and	x1, x1, #0x3c
  860f50:      	lsl	x1, x13, x1
  860f54:      	bics	x0, x0, x1
  860f58:      	b.ne	0x860f24 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x124>
  860f5c:      	ldur	w17, [x17, #0x1]
  860f60:      	cbz	w17, 0x860e80 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  860f64:      	ldp	x17, x1, [x28, #0x78]
  860f68:      	eor	x18, x17, x27
  860f6c:      	ubfx	x0, x18, #6, #34
  860f70:      	eor	x17, x0, x1
  860f74:      	mul	w1, w17, w25
  860f78:      	umulh	x17, x17, x25
  860f7c:      	eor	w1, w18, w1
  860f80:      	eor	w17, w1, w17
  860f84:      	ldr	x1, [x28, #0x40]
  860f88:      	ubfiz	x17, x17, #7, #6
  860f8c:      	add	x17, x1, x17
  860f90:      	ldrb	w1, [x17]
  860f94:      	cbz	w1, 0x861034 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x234>
  860f98:      	ldr	x2, [x28, #0x88]
  860f9c:      	ubfx	x18, x18, #14, #26
  860fa0:      	ldur	d0, [x17, #0x9]
  860fa4:      	movi	v1.2d, #0000000000000000
  860fa8:      	cmp	w1, #0x9
  860fac:      	csel	w1, w1, w11, lo
  860fb0:      	lsl	w1, w1, #2
  860fb4:      	lsl	x1, x12, x1
  860fb8:      	eor	x2, x2, x18
  860fbc:      	mul	w3, w2, w25
  860fc0:      	umulh	x2, x2, x25
  860fc4:      	eor	w0, w0, w3
  860fc8:      	eor	w0, w0, w2
  860fcc:      	ldrb	w2, [x17, #0x11]
  860fd0:      	mov	v1.s[0], w2
  860fd4:      	mov	v0.d[1], v1.d[0]
  860fd8:      	dup	v1.16b, w0
  860fdc:      	cmeq	v0.16b, v1.16b, v0.16b
  860fe0:      	shrn	v0.8b, v0.8h, #0x4
  860fe4:      	fmov	x0, d0
  860fe8:      	bics	x0, x0, x1
  860fec:      	b.eq	0x861034 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x234>
  860ff0:      	nop
  860ff4:      	nop
  860ff8:      	nop
  860ffc:      	nop
  861000:      	rbit	x1, x0
  861004:      	clz	x1, x1
  861008:      	lsr	w2, w1, #2
  86100c:      	lsl	w3, w2, #5
  861010:      	add	w3, w3, #0x90
  861014:      	lsr	x3, x3, #3
  861018:      	ldr	w3, [x17, x3]
  86101c:      	cmp	x18, x3
  861020:      	b.eq	0x8611bc <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3bc>
  861024:      	and	x1, x1, #0x3c
  861028:      	lsl	x1, x13, x1
  86102c:      	bics	x0, x0, x1
  861030:      	b.ne	0x861000 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x200>
  861034:      	ldur	w18, [x17, #0x1]
  861038:      	cbz	w18, 0x860e80 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  86103c:      	ldur	w22, [x17, #0x5]
  861040:      	cbnz	w22, 0x861050 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x250>
  861044:      	b	0x860e80 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  861048:      	ldur	w22, [x17, #0x2]
  86104c:      	cbz	w22, 0x860e80 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x80>
  861050:      	adrp	x17, 0x11c1000 <ht::prepared_field<3u>+0x198>
  861054:      	add	x17, x17, #0x370
  861058:      	ldr	x21, [x28, #0xa0]
  86105c:      	ldarb	w17, [x17]
  861060:      	tbz	w17, #0x0, 0x8610d0 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2d0>
  861064:      	lsr	w17, w22, #8
  861068:      	ldr	x0, [x26, #0x358]
  86106c:      	sub	w23, w17, #0x1
  861070:      	umaddl	x17, w23, w9, x21
  861074:      	ldr	w18, [x17, #0x20]
  861078:      	ldr	x17, [x17, #0x8]
  86107c:      	umaddl	x18, w18, w10, x0
  861080:      	ldrb	w0, [x18, #0xa]
  861084:      	ldrh	w18, [x18, #0x8]
  861088:      	cmp	w0, #0x0
  86108c:      	and	w0, w22, #0xff
  861090:      	csel	w18, w18, wzr, ne
  861094:      	mul	w18, w18, w0
  861098:      	add	x17, x17, x18
  86109c:      	ldrh	w18, [x17, #0x6]
  8610a0:      	cbz	x18, 0x861048 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x248>
  8610a4:      	add	x0, x17, #0xc
  8610a8:      	mov	x21, #-0xd              // =-13
  8610ac:      	ldur	w1, [x0, #-0x4]
  8610b0:      	ldrb	w2, [x0], #0xd
  8610b4:      	orr	x1, x1, x2, lsl #32
  8610b8:      	cmp	x1, x27
  8610bc:      	b.eq	0x861174 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x374>
  8610c0:      	sub	x18, x18, #0x1
  8610c4:      	sub	x21, x21, #0xd
  8610c8:      	cbnz	x18, 0x8610ac <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x2ac>
  8610cc:      	b	0x861048 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x248>
  8610d0:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  8610d4:      	add	x0, x0, #0x370
  8610d8:      	mov	x23, x8
  8610dc:      	bl	0x7130 <__cxa_guard_acquire@plt>
  8610e0:      	mov	w16, #0x78              // =120
  8610e4:      	mov	w15, #0x18              // =24
  8610e8:      	mov	w14, #0xa               // =10
  8610ec:      	mov	w13, #0xf               // =15
  8610f0:      	mov	x12, #-0x1              // =-1
  8610f4:      	mov	w11, #0x9               // =9
  8610f8:      	mov	w10, #0x2c              // =44
  8610fc:      	mov	w9, #0x28               // =40
  861100:      	mov	x8, x23
  861104:      	cbz	w0, 0x861064 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x264>
  861108:      	sub	x0, x29, #0x2
  86110c:      	mov	w26, w24
  861110:      	bl	0x85c140 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  861114:      	adrp	x0, 0x10000 <ht::record_table<8u, (ht::layout_solver::container)0, 0u, 1u, 0u, 64u, 64u, (ht::record_format)1, false, (ht::record_access)0>::record_table(orbital::allocation::Pool&, unsigned long, unsigned long)+0x110>
  861118:      	add	x0, x0, #0x720
  86111c:      	adrp	x2, 0x11c0000 <_GLOBAL_OFFSET_TABLE_+0x158>
  861120:      	add	x2, x2, #0x448
  861124:      	mov	x1, x23
  861128:      	bl	0x6e10 <__cxa_atexit@plt>
  86112c:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  861130:      	add	x0, x0, #0x370
  861134:      	bl	0x6c90 <__cxa_guard_release@plt>
  861138:      	mov	w24, w26
  86113c:      	mov	w9, #0x28               // =40
  861140:      	adrp	x26, 0x11c1000 <ht::prepared_field<3u>+0x198>
  861144:      	mov	w10, #0x2c              // =44
  861148:      	mov	w11, #0x9               // =9
  86114c:      	mov	x12, #-0x1              // =-1
  861150:      	mov	w13, #0xf               // =15
  861154:      	mov	w14, #0xa               // =10
  861158:      	mov	w15, #0x18              // =24
  86115c:      	mov	w16, #0x78              // =120
  861160:      	mov	x8, x23
  861164:      	b	0x861064 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x264>
  861168:      	lsl	w18, w2, #6
  86116c:      	add	w18, w18, #0x168
  861170:      	b	0x8611c4 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3c4>
  861174:      	adrp	x17, 0x11c1000 <ht::prepared_field<3u>+0x198>
  861178:      	add	x17, x17, #0x370
  86117c:      	ldr	x27, [x28, #0xa0]
  861180:      	ldarb	w17, [x17]
  861184:      	tbz	w17, #0x0, 0x8611ec <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3ec>
  861188:      	umaddl	x17, w23, w9, x27
  86118c:      	ldr	x0, [x26, #0x358]
  861190:      	ldr	w18, [x17, #0x20]
  861194:      	ldr	x17, [x17, #0x8]
  861198:      	umaddl	x18, w18, w10, x0
  86119c:      	ldrb	w0, [x18, #0xa]
  8611a0:      	ldrh	w18, [x18, #0x8]
  8611a4:      	cmp	w0, #0x0
  8611a8:      	and	w0, w22, #0xff
  8611ac:      	csel	x18, x18, xzr, ne
  8611b0:      	umaddl	x17, w18, w0, x17
  8611b4:      	sub	x17, x17, x21
  8611b8:      	b	0x8611cc <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x3cc>
  8611bc:      	lsl	w18, w2, #6
  8611c0:      	add	w18, w18, #0x1b0
  8611c4:      	lsr	w18, w18, #3
  8611c8:      	add	x17, x17, x18
  8611cc:      	ldr	x27, [x17]
  8611d0:      	mov	w17, #0x1               // =1
  8611d4:      	add	x20, x27, x20
  8611d8:      	add	x20, x20, x17
  8611dc:      	add	w24, w24, #0x1
  8611e0:      	cmp	w24, #0x1c, lsl #12     // =0x1c000
  8611e4:      	b.ne	0x860e94 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x94>
  8611e8:      	b	0x861284 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x484>
  8611ec:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  8611f0:      	add	x0, x0, #0x370
  8611f4:      	mov	x28, x8
  8611f8:      	bl	0x7130 <__cxa_guard_acquire@plt>
  8611fc:      	mov	w16, #0x78              // =120
  861200:      	mov	w15, #0x18              // =24
  861204:      	mov	w14, #0xa               // =10
  861208:      	mov	w13, #0xf               // =15
  86120c:      	mov	x12, #-0x1              // =-1
  861210:      	mov	w11, #0x9               // =9
  861214:      	mov	w10, #0x2c              // =44
  861218:      	mov	w9, #0x28               // =40
  86121c:      	mov	x8, x28
  861220:      	cbz	w0, 0x861188 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x388>
  861224:      	sub	x0, x29, #0x1
  861228:      	mov	w26, w24
  86122c:      	bl	0x85c140 <ht::links<40u, false, 8u, false>::formats()::'lambda'()::operator()() const>
  861230:      	adrp	x0, 0x10000 <ht::record_table<8u, (ht::layout_solver::container)0, 0u, 1u, 0u, 64u, 64u, (ht::record_format)1, false, (ht::record_access)0>::record_table(orbital::allocation::Pool&, unsigned long, unsigned long)+0x110>
  861234:      	add	x0, x0, #0x720
  861238:      	adrp	x2, 0x11c0000 <_GLOBAL_OFFSET_TABLE_+0x158>
  86123c:      	add	x2, x2, #0x448
  861240:      	mov	x1, x28
  861244:      	bl	0x6e10 <__cxa_atexit@plt>
  861248:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  86124c:      	add	x0, x0, #0x370
  861250:      	bl	0x6c90 <__cxa_guard_release@plt>
  861254:      	mov	x8, x28
  861258:      	mov	w24, w26
  86125c:      	mov	w9, #0x28               // =40
  861260:      	adrp	x26, 0x11c1000 <ht::prepared_field<3u>+0x198>
  861264:      	mov	w10, #0x2c              // =44
  861268:      	mov	w11, #0x9               // =9
  86126c:      	mov	x12, #-0x1              // =-1
  861270:      	mov	w13, #0xf               // =15
  861274:      	mov	w14, #0xa               // =10
  861278:      	mov	w15, #0x18              // =24
  86127c:      	mov	w16, #0x78              // =120
  861280:      	b	0x861188 <void ht::format_trial<40u, (ht::layout_solver::container)1, 10u, 0u>(orbital::allocation::Pool&, unsigned int)::'lambda3'()::operator()() const+0x388>
  861284:      	mov	x0, x20
  861288:      	ldp	x20, x19, [sp, #0x60]
  86128c:      	ldp	x22, x21, [sp, #0x50]
  861290:      	ldp	x24, x23, [sp, #0x40]
  861294:      	ldp	x26, x25, [sp, #0x30]
  861298:      	ldp	x28, x27, [sp, #0x20]
  86129c:      	ldp	x29, x30, [sp, #0x10]
  8612a0:      	add	sp, sp, #0x70
  8612a4:      	ret
  8612a8:      	mov	x19, x0
  8612ac:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  8612b0:      	add	x0, x0, #0x370
  8612b4:      	bl	0x6c70 <__cxa_guard_abort@plt>
  8612b8:      	mov	x0, x19
  8612bc:      	bl	0x7110 <_Unwind_Resume@plt>
  8612c0:      	mov	x19, x0
  8612c4:      	adrp	x0, 0x11c1000 <ht::prepared_field<3u>+0x198>
  8612c8:      	add	x0, x0, #0x370
  8612cc:      	bl	0x6c70 <__cxa_guard_abort@plt>
  8612d0:      	mov	x0, x19
  8612d4:      	bl	0x7110 <_Unwind_Resume@plt>
  8612d8:      	nop
  8612dc:      	nop

