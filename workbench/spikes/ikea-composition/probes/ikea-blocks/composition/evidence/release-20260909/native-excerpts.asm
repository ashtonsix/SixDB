0000000000000000 <ikea_comp_load>:
       0:      	ldp	q0, q1, [x1]
       4:      	ldr	x4, [x0], #0x8
       8:      	br	x4

0000000000000000 <ikea_comp_features>:
       0:      	cnt	v2.16b, v0.16b
       4:      	cnt	v3.16b, v1.16b
       8:      	adrp	x8, 0x0 <ikea_comp_features>
		0000000000000008:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.cst16
       c:      	ldr	q4, [x8]
		000000000000000c:  R_AARCH64_LDST128_ABS_LO12_NC	.rodata.cst16
      10:      	mov	w11, #0x80              // =128
      14:      	ldr	x4, [x0], #0x8
      18:      	tbl	v5.16b, { v4.16b }, v2.16b
      1c:      	tbl	v4.16b, { v4.16b }, v3.16b
      20:      	addv	b2, v2.16b
      24:      	addv	b3, v3.16b
      28:      	fmov	w8, s2
      2c:      	addv	b5, v5.16b
      30:      	addv	b4, v4.16b
      34:      	fmov	w9, s3
      38:      	add	w8, w9, w8, uxtb
      3c:      	fmov	w9, s5
      40:      	fmov	w10, s4
      44:      	add	w9, w10, w9, uxtb
      48:      	sub	w10, w8, #0x80
      4c:      	subs	w8, w11, w8
      50:      	csel	w8, w8, w10, hi
      54:      	orr	w3, w8, w9, lsl #16
      58:      	br	x4

000000000000005c <ikea_comp_load_features>:
      5c:      	ldp	q0, q1, [x1]
      60:      	adrp	x8, 0x0 <ikea_comp_features>
		0000000000000060:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.cst16+0x10
      64:      	ldr	q4, [x8]
		0000000000000064:  R_AARCH64_LDST128_ABS_LO12_NC	.rodata.cst16+0x10
      68:      	mov	w11, #0x80              // =128
      6c:      	ldr	x4, [x0], #0x8
      70:      	cnt	v2.16b, v0.16b
      74:      	cnt	v3.16b, v1.16b
      78:      	tbl	v5.16b, { v4.16b }, v2.16b
      7c:      	tbl	v4.16b, { v4.16b }, v3.16b
      80:      	addv	b2, v2.16b
      84:      	addv	b3, v3.16b
      88:      	fmov	w8, s2
      8c:      	addv	b5, v5.16b
      90:      	addv	b4, v4.16b
      94:      	fmov	w9, s3
      98:      	add	w8, w9, w8, uxtb
      9c:      	fmov	w9, s5
      a0:      	fmov	w10, s4
      a4:      	add	w9, w10, w9, uxtb
      a8:      	sub	w10, w8, #0x80
      ac:      	subs	w8, w11, w8
      b0:      	csel	w8, w8, w10, hi
      b4:      	orr	w3, w8, w9, lsl #16
      b8:      	br	x4

0000000000000000 <ikea_comp_model>:
       0:      	and	w8, w3, #0xff
       4:      	cmp	w8, #0x80
       8:      	b.ne	0x18 <ikea_comp_model+0x18>
       c:      	ldr	x4, [x0], #0x8
      10:      	mov	x3, xzr
      14:      	br	x4
      18:      	mov	w9, #0x80               // =128
      1c:      	adrp	x11, 0x0 <ikea_comp_model>
		000000000000001c:  R_AARCH64_ADR_PREL_PG_HI21	_ZN4ikea13bec_predictor12coefficientsE
      20:      	add	x11, x11, #0x0
		0000000000000020:  R_AARCH64_ADD_ABS_LO12_NC	_ZN4ikea13bec_predictor12coefficientsE
      24:      	sub	w9, w9, w8
      28:      	ldr	x4, [x0], #0x8
      2c:      	cmp	w9, #0x8
      30:      	cset	w10, hi
      34:      	cmp	w9, #0x20
      38:      	cinc	x10, x10, hi
      3c:      	cmp	w9, #0x60
      40:      	cinc	x9, x10, hi
      44:      	mov	w10, #0xc               // =12
      48:      	umaddl	x9, w9, w10, x11
      4c:      	ubfx	w11, w3, #16, #8
      50:      	ldp	w10, w12, [x9, #0x4]
      54:      	ldr	w9, [x9]
      58:      	madd	w8, w10, w8, w9
      5c:      	madd	w8, w12, w11, w8
      60:      	add	w8, w8, #0x800
      64:      	asr	w9, w8, #12
      68:      	bic	w8, w9, w8, asr #31
      6c:      	mov	w9, #0x2f               // =47
      70:      	cmp	w8, #0x2f
      74:      	csel	w3, w8, w9, lt
      78:      	br	x4

000000000000007c <ikea_comp_transition_model_stage>:
      7c:      	and	w8, w3, #0xff
      80:      	cmp	w8, #0x80
      84:      	b.ne	0x94 <ikea_comp_transition_model_stage+0x18>
      88:      	ldr	x4, [x0], #0x8
      8c:      	mov	x3, xzr
      90:      	br	x4
      94:      	ubfx	w9, w3, #16, #8
      98:      	ubfx	w10, w3, #8, #8
      9c:      	cbz	w9, 0xc4 <ikea_comp_transition_model_stage+0x48>
      a0:      	mov	w11, #0x80              // =128
      a4:      	sub	w11, w11, w8
      a8:      	cmp	w11, #0x8
      ac:      	cset	w12, hi
      b0:      	cmp	w11, #0x20
      b4:      	cinc	x12, x12, hi
      b8:      	cmp	w11, #0x60
      bc:      	cinc	x11, x12, hi
      c0:      	b	0xc8 <ikea_comp_transition_model_stage+0x4c>
      c4:      	mov	w11, #0x4               // =4
      c8:      	mov	w12, #0x14              // =20
      cc:      	adrp	x13, 0x0 <ikea_comp_model>
		00000000000000cc:  R_AARCH64_ADR_PREL_PG_HI21	_ZN4ikea13bec_predictor23transition_coefficientsE
      d0:      	add	x13, x13, #0x0
		00000000000000d0:  R_AARCH64_ADD_ABS_LO12_NC	_ZN4ikea13bec_predictor23transition_coefficientsE
      d4:      	umaddl	x11, w11, w12, x13
      d8:      	ldr	x4, [x0], #0x8
      dc:      	ldp	w12, w13, [x11, #0x4]
      e0:      	ldr	w14, [x11]
      e4:      	ldr	w11, [x11, #0xc]
      e8:      	madd	w8, w12, w8, w14
      ec:      	madd	w8, w13, w10, w8
      f0:      	madd	w8, w11, w9, w8
      f4:      	add	w8, w8, #0x800
      f8:      	asr	w9, w8, #12
      fc:      	bic	w8, w9, w8, asr #31
     100:      	mov	w9, #0x2f               // =47
     104:      	cmp	w8, #0x2f
     108:      	csel	w3, w8, w9, lt
     10c:      	br	x4

0000000000000000 <ikea_comp_done>:
       0:      	add	x0, x3, x2
       4:      	ret
