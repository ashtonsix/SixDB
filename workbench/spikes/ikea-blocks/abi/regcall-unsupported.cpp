// Compile separately with -Werror=ignored-attributes on AArch64. An ignored
// calling convention must not be mistaken for a usable alternate stage ABI.
extern "C" __attribute__((regcall)) unsigned unsupported(unsigned x) { return x; }
