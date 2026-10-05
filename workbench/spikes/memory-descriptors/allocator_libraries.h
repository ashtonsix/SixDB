#pragma once
#include <cstddef>

namespace allocator_libraries {
void* mi_allocate(std::size_t bytes);
void mi_release(void* pointer);
std::size_t mi_capacity(void* pointer);
void* sn_allocate(std::size_t bytes);
void sn_release(void* pointer);
std::size_t sn_capacity(void* pointer);
}
