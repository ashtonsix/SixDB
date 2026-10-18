.intel_syntax noprefix
.text
# LLVM-MCA-BEGIN equal6_bounded
	mov	ecx, dword ptr [rdi + 2]
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI5_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	mov	eax, dword ptr [rdi]
	vpbroadcastb	xmm0, esi
	shl	rcx, 16
	or	rcx, rax
	vpbroadcastq	xmm2, rcx
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	vgf2p8affineqb	xmm1, xmm1, xmm2, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI5_1]
	kmovb	eax, k0
# LLVM-MCA-END equal6_bounded
# LLVM-MCA-BEGIN equal6_owner
	vpbroadcastq	xmm1, qword ptr [rip + .LCPI6_0] # xmm1 = [1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128]
	vpbroadcastb	xmm0, esi
	vgf2p8affineqb	xmm0, xmm0, xmm1, 0
	vgf2p8affineqb	xmm1, xmm1, qword ptr [rdi]{1to2}, 0
	vpxor	xmm0, xmm1, xmm0
	vpcmpltub	k0, xmm0, xmmword ptr [rip + .LCPI6_1]
	kmovb	eax, k0
# LLVM-MCA-END equal6_owner
