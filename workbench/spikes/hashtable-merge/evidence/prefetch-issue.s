# LLVM-MCA-BEGIN narrow_before
prefetchw 0(%rdi)
prefetchw 64(%rdi)
prefetchw 128(%rdi)
# LLVM-MCA-END narrow_before
# LLVM-MCA-BEGIN narrow_after
prefetchw 0(%rdi)
prefetchw 64(%rdi)
prefetchw 128(%rdi)
# LLVM-MCA-END narrow_after
# LLVM-MCA-BEGIN wide8_before
prefetchw 0(%rdi)
prefetchw 96(%rdi)
# LLVM-MCA-END wide8_before
# LLVM-MCA-BEGIN wide8_after
prefetchw 0(%rdi)
prefetchw 64(%rdi)
prefetchw 128(%rdi)
# LLVM-MCA-END wide8_after
# LLVM-MCA-BEGIN wide16_before
prefetchw 0(%rdi)
prefetchw 96(%rdi)
prefetchw 160(%rdi)
# LLVM-MCA-END wide16_before
# LLVM-MCA-BEGIN wide16_after
prefetchw 0(%rdi)
prefetchw 64(%rdi)
prefetchw 128(%rdi)
prefetchw 192(%rdi)
# LLVM-MCA-END wide16_after
# LLVM-MCA-BEGIN wide_set_before
prefetchw 0(%rdi)
# LLVM-MCA-END wide_set_before
# LLVM-MCA-BEGIN wide_set_after
prefetchw 0(%rdi)
prefetchw 64(%rdi)
# LLVM-MCA-END wide_set_after
