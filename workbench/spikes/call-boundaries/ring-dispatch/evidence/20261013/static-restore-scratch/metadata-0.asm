00000000000008a0 <metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])>:
     8a0: f94052a8     	ldr	x8, [x21, #0xa0]
     8a4: ad009aa7     	stp	q7, q6, [x21, #0x10]
     8a8: 90000009     	adrp	x9, 0x0 <printf@GLIBC_2.17>
     8ac: 91248129     	add	x9, x9, #0x920
     8b0: ad0192a5     	stp	q5, q4, [x21, #0x30]
     8b4: f90002a9     	str	x9, [x21]
     8b8: 8b0802e8     	add	x8, x23, x8
     8bc: ad028aa3     	stp	q3, q2, [x21, #0x50]
     8c0: ad0382a1     	stp	q1, q0, [x21, #0x70]
     8c4: 91003508     	add	x8, x8, #0xd
     8c8: a9095aa8     	stp	x8, x22, [x21, #0x90]
     8cc: b9458688     	ldr	w8, [x20, #0x584]
     8d0: 34000228     	cbz	w8, 0x914 <metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x74>
     8d4: b9458288     	ldr	w8, [x20, #0x580]
     8d8: 52801609     	mov	w9, #0xb0               // =176
     8dc: d503201f     	nop
     8e0: 9ba95115     	umaddl	x21, w8, w9, x20
     8e4: 11000508     	add	w8, w8, #0x1
     8e8: 12000908     	and	w8, w8, #0x7
     8ec: f9404eb6     	ldr	x22, [x21, #0x98]
     8f0: b4ffff96     	cbz	x22, 0x8e0 <metadata(Ring*, Slot*, unsigned long, unsigned long, unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2], unsigned long vector[2])+0x40>
     8f4: ad4382a1     	ldp	q1, q0, [x21, #0x70]
     8f8: ad428aa3     	ldp	q3, q2, [x21, #0x50]
     8fc: ad4192a5     	ldp	q5, q4, [x21, #0x30]
     900: f94002a0     	ldr	x0, [x21]
     904: f9404ab7     	ldr	x23, [x21, #0x90]
     908: ad409aa7     	ldp	q7, q6, [x21, #0x10]
     90c: b9058288     	str	w8, [x20, #0x580]
     910: d61f0000     	br	x0
     914: d65f03c0     	ret
     918: d503201f     	nop
     91c: d503201f     	nop

