.intel_syntax noprefix
.text
# LLVM-MCA-BEGIN equal6_bounded
	movabs	rax, 72340172838076673
	mov	edx, dword ptr [rdi + 2]
	pdep	rax, rsi, rax
	mov	rcx, rax
	shl	rcx, 8
	sub	rcx, rax
	mov	eax, dword ptr [rdi]
	shl	rdx, 16
	or	rdx, rax
	xor	rdx, rcx
	not	rdx
	mov	rax, rdx
	shr	rax, 16
	and	eax, edx
	mov	ecx, eax
	shr	ecx, 16
	and	ecx, eax
	mov	eax, ecx
	shr	eax, 8
	and	eax, ecx
# LLVM-MCA-END equal6_bounded
# LLVM-MCA-BEGIN equal6_owner
	movabs	rax, 72340172838076673
	pdep	rax, rsi, rax
	mov	rcx, rax
	shl	rcx, 8
	sub	rcx, rax
	xor	rcx, qword ptr [rdi]
	not	rcx
	mov	rax, rcx
	shr	rax, 16
	and	eax, ecx
	mov	ecx, eax
	shr	ecx, 16
	and	ecx, eax
	mov	eax, ecx
	shr	eax, 8
	and	eax, ecx
# LLVM-MCA-END equal6_owner
