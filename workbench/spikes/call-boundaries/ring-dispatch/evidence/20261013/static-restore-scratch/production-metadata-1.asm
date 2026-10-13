0000000000003b00 <metadata(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul>, std::tuple<>, Frame*>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul>, std::tuple<>, Frame*>::Slot*, Frame*)>:
    3b00: f94002e8     	ldr	x8, [x23]
    3b04: f9400ae9     	ldr	x9, [x23, #0x10]
    3b08: 8b090108     	add	x8, x8, x9
    3b0c: 91003508     	add	x8, x8, #0xd
    3b10: f9000ae8     	str	x8, [x23, #0x10]
    3b14: 90000008     	adrp	x8, 0x3000 <uname@GLIBC_2.17+0x3000>
    3b18: 912d4108     	add	x8, x8, #0xb50
    3b1c: f90002c8     	str	x8, [x22]
    3b20: b40000f5     	cbz	x21, 0x3b3c <metadata(loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul>, std::tuple<>, Frame*>::Cursor, loom::detail::MemoryRing<std::integer_sequence<unsigned long>, std::integer_sequence<unsigned long, 0ul>, std::tuple<>, Frame*>::Slot*, Frame*)+0x3c>
    3b24: f9405296     	ldr	x22, [x20, #0xa0]
    3b28: d10006b5     	sub	x21, x21, #0x1
    3b2c: a94022c0     	ldp	x0, x8, [x22]
    3b30: f9400ad7     	ldr	x23, [x22, #0x10]
    3b34: f9005288     	str	x8, [x20, #0xa0]
    3b38: d61f0000     	br	x0
    3b3c: f900869f     	str	xzr, [x20, #0x108]
    3b40: d65f03c0     	ret
    3b44: d503201f     	nop
    3b48: d503201f     	nop
    3b4c: d503201f     	nop

