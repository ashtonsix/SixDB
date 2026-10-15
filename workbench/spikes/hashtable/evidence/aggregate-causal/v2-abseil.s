# Actual primary/first-group hit, first candidate matches; repeated operation body.
# Branch directions linearized, branch instructions preserved.
# LLVM-MCA-BEGIN v2-abseil
ldp	x8, x23, [x20]
ldr	x12, [x22]
stur	x8, [x29, #-0x8]
tst	x12, #0x3e
b.eq	.Ledge
lsl	x11, x27, x12
and	x12, x12, #0x7c0
ldr	x9, [x21, #0x28]
mov	x5, xzr
eor	x12, x12, x8
mul	x13, x12, x28
umulh	x12, x12, x28
sub	x10, x9, x11
mvn	x11, x11
prfm	pldl3keep, [x9]
eor	x2, x12, x13
add	x10, x10, #0x7
lsr	x12, x2, #57
dup	v0.8b, w12
mov	x12, x2
and	x4, x12, x11
lsl	x12, x4, #4
prfm	pldl1keep, [x10, x12]
ldr	d1, [x9, x4]
cmeq	v2.8b, v1.8b, v0.8b
fmov	x12, d2
cbz	x12, .Ledge
rbit	x13, x12
clz	x13, x13
add	x13, x4, x13, lsr #3
and	x13, x13, x11
lsl	x14, x13, #4
ldr	x14, [x10, x14]
cmp	x14, x8
b.eq	.Ledge
add	x9, x10, x13, lsl #4
ldr	x10, [x9, #0x8]
add	x10, x10, x23
str	x10, [x9, #0x8]
add	x19, xzr, x19
add	x20, x20, #0x10
cmp	x20, x25
b.ne	.Ledge
# LLVM-MCA-END
.Ledge:
