# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN v2-fuller-hot
ldp	x2, x3, [x20]
ldp	x8, x10, [x21, #0x40]
eor	x22, x8, x2
ubfx	x8, x22, #8, #32
ubfx	x9, x22, #16, #24
and	x17, x22, #0xffffffffff
eor	x10, x8, x10
mul	w11, w10, w27
umulh	x10, x10, x27
eor	w11, w22, w11
eor	w10, w11, w10
ldr	x11, [x21, #0x50]
ubfiz	x10, x10, #7, #8
eor	x11, x9, x11
mul	w12, w11, w27
umulh	x11, x11, x27
eor	w12, w8, w12
ldr	x8, [x21, #0x8]
eor	w18, w12, w11
dup	v1.16b, w18
add	x16, x8, x10
mov	x0, x16
ldr	q0, [x0, #0x5]!
mov	x24, x16
ldrb	w8, [x24], #0x15
ubfiz	x11, x8, #2, #32
lsl	x11, x28, x11
cmeq	v0.16b, v0.16b, v1.16b
shrn	v0.8b, v0.8h, #0x4
fmov	x10, d0
bic	x10, x10, x11
cbz	x10, .Ledge
rbit	x11, x10
clz	x12, x11
lsr	w11, w12, #2
and	x12, x12, #0x3c
lsl	x12, x25, x12
bic	x10, x10, x12
mov	w12, w11
add	x12, x12, w11, uxtw #1
add	x12, x24, x12
ldrh	w13, [x12]
ldrb	w12, [x12, #0x2]
orr	x12, x13, x12, lsl #16
cmp	x12, x9
b.ne	.Ledge
add	x9, x16, w11, uxtw #3
mov	x8, xzr
ldr	x10, [x9, #0x30]
add	x10, x10, x3
str	x10, [x9, #0x30]
b	.Ledge
add	x23, x8, x23
add	x20, x20, #0x10
cmp	x20, x26
b.eq	.Ledge
# LLVM-MCA-END
.Ledge:
