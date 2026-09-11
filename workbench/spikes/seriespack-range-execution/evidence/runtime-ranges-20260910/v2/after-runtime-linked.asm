0000000000087120 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
   87208:      	bl	0x26c298 <benchmark::State::StartKeepRunning()>
   8720c:      	cbnz	w21, 0x8728c <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x16c>
   87210:      	cbz	x24, 0x8728c <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x16c>
   87214:      	mov	x25, xzr
   87218:      	add	x26, sp, #0x14
   8721c:      	add	x27, sp, #0x8
   87220:      	mov	w28, #0x100             // =256
   87224:      	add	x8, x23, x25, lsl #4
   87228:      	ldp	x21, x22, [x8]
   8722c:      	ldr	x8, [sp, #0x110]
   87230:      	add	x0, sp, #0xa0
   87234:      	add	x3, sp, #0x14
   87238:      	mov	w4, #0x20               // =32
   8723c:      	mov	x1, x21
   87240:      	mov	x2, x22
   87244:      	blr	x8
   87248:      	add	x8, x25, #0x1
   8724c:      	mvn	x9, x21
   87250:      	ldr	x10, [sp, #0x8]
   87254:      	cmp	x8, #0x1, lsl #12       // =0x1000
   87258:      	ldr	w8, [sp, #0x14]
   8725c:      	add	x9, x22, x9
   87260:      	ldr	w9, [x26, x9, lsl #2]
   87264:      	csinc	x25, xzr, x25, eq
   87268:      	subs	x28, x28, #0x1
   8726c:      	add	x8, x8, x10
   87270:      	add	x8, x9, x8
   87274:      	str	x8, [sp, #0x8]
   87278:      	b.ne	0x87224 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x104>
   8727c:      	cmp	x24, #0x0
   87280:      	b.le	0x87a00 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x8e0>
   87284:      	subs	x24, x24, #0x1
   87288:      	b.ne	0x87220 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x100>
   8728c:      	mov	x0, x19
   87290:      	bl	0x26c354 <benchmark::State::FinishKeepRunning()>

0000000000088b80 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
   88c68:      	bl	0x26c298 <benchmark::State::StartKeepRunning()>
   88c6c:      	cbnz	w21, 0x88ce8 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x168>
   88c70:      	cbz	x24, 0x88ce8 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x168>
   88c74:      	mov	x25, xzr
   88c78:      	add	x26, sp, #0x10
   88c7c:      	add	x27, sp, #0x8
   88c80:      	mov	w28, #0x100             // =256
   88c84:      	add	x8, x23, x25, lsl #4
   88c88:      	ldp	x21, x22, [x8]
   88c8c:      	ldr	x8, [sp, #0x150]
   88c90:      	add	x0, sp, #0xe0
   88c94:      	add	x3, sp, #0x10
   88c98:      	mov	w4, #0x40               // =64
   88c9c:      	mov	x1, x21
   88ca0:      	mov	x2, x22
   88ca4:      	blr	x8
   88ca8:      	add	x8, x25, #0x1
   88cac:      	mvn	x9, x21
   88cb0:      	cmp	x8, #0x1, lsl #12       // =0x1000
   88cb4:      	ldp	x10, x8, [sp, #0x8]
   88cb8:      	add	x9, x22, x9
   88cbc:      	ldr	x9, [x26, x9, lsl #3]
   88cc0:      	csinc	x25, xzr, x25, eq
   88cc4:      	subs	x28, x28, #0x1
   88cc8:      	add	x8, x8, x10
   88ccc:      	add	x8, x9, x8
   88cd0:      	str	x8, [sp, #0x8]
   88cd4:      	b.ne	0x88c84 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x104>
   88cd8:      	cmp	x24, #0x0
   88cdc:      	b.le	0x89444 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x8c4>
   88ce0:      	subs	x24, x24, #0x1
   88ce4:      	b.ne	0x88c80 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x100>
   88ce8:      	mov	x0, x19
   88cec:      	bl	0x26c354 <benchmark::State::FinishKeepRunning()>
