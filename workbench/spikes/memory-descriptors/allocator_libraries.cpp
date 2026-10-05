#include "allocator_libraries.h"
#include <mimalloc.h>
#include <snmalloc/snmalloc.h>

namespace allocator_libraries {
void* mi_allocate(std::size_t bytes) { return mi_malloc(bytes); }
void mi_release(void* pointer) { mi_free(pointer); }
std::size_t mi_capacity(void* pointer) { return mi_usable_size(pointer); }
void* sn_allocate(std::size_t bytes) { return snmalloc::alloc(bytes); }
void sn_release(void* pointer) { snmalloc::dealloc(pointer); }
std::size_t sn_capacity(void* pointer) { return snmalloc::alloc_size(pointer); }
}
