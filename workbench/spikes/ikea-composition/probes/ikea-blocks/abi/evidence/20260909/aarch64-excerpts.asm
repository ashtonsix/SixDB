0000000000000000 <raw128>:
       0:      	ldr	q0, [x0]
       4:      	ret

000000000000001c <expected128>:
      1c:      	tbz	w1, #0x0, 0x34 <expected128+0x18>
      20:      	ldr	q0, [x0]
      24:      	mov	w9, #0x1                // =1
      28:      	strb	w9, [x8, #0x10]
      2c:      	str	q0, [x8]
      30:      	ret
      34:      	mov	w10, #0x3               // =3
      38:      	strb	wzr, [x8, #0x10]
      3c:      	strb	w10, [x8]
      40:      	ret

000000000000005c <call_expected128>:
      5c:      	sub	sp, sp, #0x40
      60:      	stp	x29, x30, [sp, #0x20]
      64:      	str	x19, [sp, #0x30]
      68:      	add	x29, sp, #0x20
      6c:      	mov	x8, sp
      70:      	mov	x19, x2
      74:      	bl	0x74 <call_expected128+0x18>
		0000000000000074:  R_AARCH64_CALL26	expected128
      78:      	ldrb	w8, [sp, #0x10]
      7c:      	tbz	w8, #0x0, 0x9c <call_expected128+0x40>
      80:      	ldr	q0, [sp]
      84:      	mov	w0, wzr
      88:      	str	q0, [x19]
      8c:      	ldp	x29, x30, [sp, #0x20]
      90:      	ldr	x19, [sp, #0x30]
      94:      	add	sp, sp, #0x40
      98:      	ret
      9c:      	ldrb	w0, [sp]
      a0:      	ldp	x29, x30, [sp, #0x20]
      a4:      	ldr	x19, [sp, #0x30]
      a8:      	add	sp, sp, #0x40
      ac:      	ret

00000000000000d0 <raw512>:
      d0:      	ldp	q1, q0, [x0, #0x20]
      d4:      	stp	q1, q0, [x8, #0x20]
      d8:      	ldp	q2, q0, [x0]
      dc:      	stp	q2, q0, [x8]
      e0:      	ret

0000000000000130 <cps512>:
     130:      	sub	sp, sp, #0x50
     134:      	stp	x29, x30, [sp, #0x40]
     138:      	add	x29, sp, #0x40
     13c:      	ldp	q1, q0, [x0, #0x20]
     140:      	mov	x8, x2
     144:      	add	x1, x1, #0x3
     148:      	mov	x2, sp
     14c:      	stp	q1, q0, [sp, #0x20]
     150:      	ldp	q2, q0, [x0]
     154:      	mov	x0, x8
     158:      	stp	q2, q0, [sp]
     15c:      	blr	x3
     160:      	ldp	x29, x30, [sp, #0x40]
     164:      	add	sp, sp, #0x50
     168:      	ret

000000000000018c <cps_chunks512>:
     18c:      	ldp	q0, q1, [x0]
     190:      	add	x1, x1, #0x3
     194:      	ldp	q2, q3, [x0, #0x20]
     198:      	mov	x0, x2
     19c:      	br	x3
