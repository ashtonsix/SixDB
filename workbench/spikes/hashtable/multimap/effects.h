#pragma once
#include <ikea/effects.h>
#include <ikea/hashtable/mutation.h>

namespace write_arch {
// A captured allocation identity for retirement and beforeimage qualification.
// Writes themselves use journal::entry and refer to the stable material object;
// this byte extent is kept separately because retirement can destroy its owner.
struct physical_region {
    const void* source=nullptr;
    unsigned plane=0;
    ikea::bytes storage;
    physical_region()=default;
    physical_region(const void* owner, unsigned id, const void* base, std::size_t extent)
        : source(owner), plane(id), storage(static_cast<const std::byte*>(base), extent) {}
    physical_region(const void* owner, ikea::hashtable::plane id, const void* base, std::size_t extent)
        : physical_region(owner, unsigned(id), base, extent) {}
    bool operator==(const physical_region& other) const noexcept {
        return source == other.source && plane == other.plane &&
            storage.data() == other.storage.data() && storage.size() == other.storage.size();
    }
};
inline ikea::journal::entry write_effect(const physical_region& region, std::size_t offset,
                                        std::size_t size) noexcept {
    return {region.source, region.plane, offset, size};
}
} // namespace write_arch
