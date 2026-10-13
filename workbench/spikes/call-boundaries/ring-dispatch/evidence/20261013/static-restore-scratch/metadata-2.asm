00000000000017a0 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>:
    17a0: f9404aa8     	ldr	x8, [x21, #0x90]
    17a4: f94052a9     	ldr	x9, [x21, #0xa0]
    17a8: 8b090108     	add	x8, x8, x9
    17ac: 90000009     	adrp	x9, 0x1000 <main+0x540>
    17b0: 91180129     	add	x9, x9, #0x600
    17b4: 91003508     	add	x8, x8, #0xd
    17b8: f90002a9     	str	x9, [x21]
    17bc: f9004aa8     	str	x8, [x21, #0x90]
    17c0: b9458688     	ldr	w8, [x20, #0x584]
    17c4: 34000328     	cbz	w8, 0x1828 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x88>
    17c8: b9458288     	ldr	w8, [x20, #0x580]
    17cc: 52801609     	mov	w9, #0xb0               // =176
    17d0: d503201f     	nop
    17d4: d503201f     	nop
    17d8: d503201f     	nop
    17dc: d503201f     	nop
    17e0: 9ba95115     	umaddl	x21, w8, w9, x20
    17e4: 11000508     	add	w8, w8, #0x1
    17e8: 12000908     	and	w8, w8, #0x7
    17ec: f9404eaa     	ldr	x10, [x21, #0x98]
    17f0: b4ffff8a     	cbz	x10, 0x17e0 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x40>
    17f4: f94002a0     	ldr	x0, [x21]
    17f8: b9058288     	str	w8, [x20, #0x580]
    17fc: 6f00e400     	movi	v0.2d, #0000000000000000
    1800: 6f00e401     	movi	v1.2d, #0000000000000000
    1804: 6f00e402     	movi	v2.2d, #0000000000000000
    1808: 6f00e403     	movi	v3.2d, #0000000000000000
    180c: 6f00e404     	movi	v4.2d, #0000000000000000
    1810: 6f00e405     	movi	v5.2d, #0000000000000000
    1814: 6f00e406     	movi	v6.2d, #0000000000000000
    1818: 6f00e407     	movi	v7.2d, #0000000000000000
    181c: aa1f03f6     	mov	x22, xzr
    1820: aa1f03f7     	mov	x23, xzr
    1824: d61f0000     	br	x0
    1828: d65f03c0     	ret

Disassembly of section .fini:

