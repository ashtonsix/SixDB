.text
// LLVM-MCA-BEGIN prepared
ldr x8, [x0]
eon x8, x1, x8
and x8, x8, x8, lsr #16
and w8, w8, w8, lsr #16
and w9, w8, w8, lsr #8
// LLVM-MCA-END prepared
// LLVM-MCA-BEGIN runtime
// Reusable weights v1 already live; runtime q in w1.
dup v0.8b, w1
cmtst v0.8b, v0.8b, v1.8b
fmov x9, d0
ldr x8, [x0]
eon x8, x9, x8
and x8, x8, x8, lsr #16
and w8, w8, w8, lsr #16
and w9, w8, w8, lsr #8
// LLVM-MCA-END runtime
