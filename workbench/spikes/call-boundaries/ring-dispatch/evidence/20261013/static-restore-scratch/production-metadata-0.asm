0000000000003b00 <metadata(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Slot*, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>:
    3b00: f94002e8     	ldr	x8, [x23]
    3b04: 8b080328     	add	x8, x25, x8
    3b08: 91003519     	add	x25, x8, #0xd
    3b0c: 90000008     	adrp	x8, 0x3000 <uname@GLIBC_2.17+0x3000>
    3b10: 912e0108     	add	x8, x8, #0xb80
    3b14: f90002c8     	str	x8, [x22]
    3b18: 394326c8     	ldrb	w8, [x22, #0xc9]
    3b1c: 7100051f     	cmp	w8, #0x1
    3b20: 54000041     	b.ne	0x3b28 <metadata(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Slot*, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x28>
    3b24: 1400054f     	b	0x5060 <loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::finish(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Slot*, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>
    3b28: a909ded8     	stp	x24, x23, [x22, #0x98]
    3b2c: f9004ad9     	str	x25, [x22, #0x90]
    3b30: ad009ac7     	stp	q7, q6, [x22, #0x10]
    3b34: ad0192c5     	stp	q5, q4, [x22, #0x30]
    3b38: ad028ac3     	stp	q3, q2, [x22, #0x50]
    3b3c: ad0382c1     	stp	q1, q0, [x22, #0x70]
    3b40: b4000195     	cbz	x21, 0x3b70 <metadata(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul, 1ul, 2ul, 3ul, 4ul, 5ul, 6ul, 7ul, 8ul, 9ul, 10ul>, std::tuple<>, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2]>::Slot*, Frame*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x70>
    3b44: f9405296     	ldr	x22, [x20, #0xa0]
    3b48: d10006b5     	sub	x21, x21, #0x1
    3b4c: a94022c0     	ldp	x0, x8, [x22]
    3b50: a949ded8     	ldp	x24, x23, [x22, #0x98]
    3b54: f9404ad9     	ldr	x25, [x22, #0x90]
    3b58: f9005288     	str	x8, [x20, #0xa0]
    3b5c: ad4382c1     	ldp	q1, q0, [x22, #0x70]
    3b60: ad428ac3     	ldp	q3, q2, [x22, #0x50]
    3b64: ad4192c5     	ldp	q5, q4, [x22, #0x30]
    3b68: ad409ac7     	ldp	q7, q6, [x22, #0x10]
    3b6c: d61f0000     	br	x0
    3b70: f900869f     	str	xzr, [x20, #0x108]
    3b74: d65f03c0     	ret
    3b78: d503201f     	nop
    3b7c: d503201f     	nop

