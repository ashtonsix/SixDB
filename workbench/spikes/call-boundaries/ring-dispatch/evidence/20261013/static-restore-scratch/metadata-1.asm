0000000000001760 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>:
    1760: a9495aa8     	ldp	x8, x22, [x21, #0x90]
    1764: f94052a9     	ldr	x9, [x21, #0xa0]
    1768: ad4382a1     	ldp	q1, q0, [x21, #0x70]
    176c: ad428aa3     	ldp	q3, q2, [x21, #0x50]
    1770: 8b090108     	add	x8, x8, x9
    1774: 90000009     	adrp	x9, 0x1000 <main+0x590>
    1778: 91170129     	add	x9, x9, #0x5c0
    177c: ad4192a5     	ldp	q5, q4, [x21, #0x30]
    1780: ad409aa7     	ldp	q7, q6, [x21, #0x10]
    1784: 91003517     	add	x23, x8, #0xd
    1788: b9458688     	ldr	w8, [x20, #0x584]
    178c: f90002a9     	str	x9, [x21]
    1790: f9004ab7     	str	x23, [x21, #0x90]
    1794: 34000168     	cbz	w8, 0x17c0 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x60>
    1798: b9458288     	ldr	w8, [x20, #0x580]
    179c: 52801609     	mov	w9, #0xb0               // =176
    17a0: 9ba95115     	umaddl	x21, w8, w9, x20
    17a4: 11000508     	add	w8, w8, #0x1
    17a8: 12000908     	and	w8, w8, #0x7
    17ac: f9404eaa     	ldr	x10, [x21, #0x98]
    17b0: b4ffff8a     	cbz	x10, 0x17a0 <void restored<&metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x40>
    17b4: f94002a0     	ldr	x0, [x21]
    17b8: b9058288     	str	w8, [x20, #0x580]
    17bc: d61f0000     	br	x0
    17c0: d65f03c0     	ret

Disassembly of section .fini:

