0000000000087200 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
   872e8:      	bl	0x26cd58 <benchmark::State::StartKeepRunning()>
   872ec:      	cbnz	w23, 0x8736c <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x16c>
   872f0:      	cbz	x22, 0x8736c <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x16c>
   872f4:      	mov	x23, xzr
   872f8:      	add	x24, sp, #0x14
   872fc:      	mov	w25, #0x20              // =32
   87300:      	add	x26, sp, #0x8
   87304:      	mov	w27, #0x100             // =256
   87308:      	add	x8, x21, x23, lsl #4
   8730c:      	ldp	x1, x2, [x8]
   87310:      	ldr	x8, [sp, #0x110]
   87314:      	stur	w25, [x29, #-0x30]
   87318:      	sub	x28, x2, x1
   8731c:      	stp	x24, x28, [x29, #-0x40]
   87320:      	add	x0, sp, #0xa0
   87324:      	sub	x3, x29, #0x40
   87328:      	blr	x8
   8732c:      	add	x8, x23, #0x1
   87330:      	add	x9, x24, x28, lsl #2
   87334:      	ldr	x10, [sp, #0x8]
   87338:      	cmp	x8, #0x1, lsl #12       // =0x1000
   8733c:      	ldr	w8, [sp, #0x14]
   87340:      	ldur	w9, [x9, #-0x4]
   87344:      	csinc	x23, xzr, x23, eq
   87348:      	subs	x27, x27, #0x1
   8734c:      	add	x8, x8, x10
   87350:      	add	x8, x9, x8
   87354:      	str	x8, [sp, #0x8]
   87358:      	b.ne	0x87308 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x108>
   8735c:      	cmp	x22, #0x0
   87360:      	b.le	0x87ae0 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x8e0>
   87364:      	subs	x22, x22, #0x1
   87368:      	b.ne	0x87304 <void seriespack_runtime_ranges::run<unsigned int>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x104>
   8736c:      	mov	x0, x19
   87370:      	bl	0x26ce14 <benchmark::State::FinishKeepRunning()>

0000000000088c60 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)>:
   88d48:      	bl	0x26cd58 <benchmark::State::StartKeepRunning()>
   88d4c:      	cbnz	w23, 0x88dc8 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x168>
   88d50:      	cbz	x22, 0x88dc8 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x168>
   88d54:      	mov	x23, xzr
   88d58:      	add	x24, sp, #0x10
   88d5c:      	mov	w25, #0x40              // =64
   88d60:      	add	x26, sp, #0x8
   88d64:      	mov	w27, #0x100             // =256
   88d68:      	add	x8, x21, x23, lsl #4
   88d6c:      	ldp	x1, x2, [x8]
   88d70:      	ldr	x8, [sp, #0x150]
   88d74:      	stur	w25, [x29, #-0x30]
   88d78:      	sub	x28, x2, x1
   88d7c:      	stp	x24, x28, [x29, #-0x40]
   88d80:      	add	x0, sp, #0xe0
   88d84:      	sub	x3, x29, #0x40
   88d88:      	blr	x8
   88d8c:      	add	x8, x23, #0x1
   88d90:      	add	x9, x24, x28, lsl #3
   88d94:      	cmp	x8, #0x1, lsl #12       // =0x1000
   88d98:      	ldp	x10, x8, [sp, #0x8]
   88d9c:      	ldur	x9, [x9, #-0x8]
   88da0:      	csinc	x23, xzr, x23, eq
   88da4:      	subs	x27, x27, #0x1
   88da8:      	add	x8, x8, x10
   88dac:      	add	x8, x9, x8
   88db0:      	str	x8, [sp, #0x8]
   88db4:      	b.ne	0x88d68 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x108>
   88db8:      	cmp	x22, #0x0
   88dbc:      	b.le	0x89524 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x8c4>
   88dc0:      	subs	x22, x22, #0x1
   88dc4:      	b.ne	0x88d64 <void seriespack_runtime_ranges::run<unsigned long>(benchmark::State&, seriespack_runtime_ranges::shape, seriespack_runtime_ranges::regime, ikea::seriespack::execution_target)+0x104>
   88dc8:      	mov	x0, x19
   88dcc:      	bl	0x26ce14 <benchmark::State::FinishKeepRunning()>
