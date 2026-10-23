#pragma once
#include <ikea/hashtable/table.h>
#include <algorithm>
#include <vector>

namespace ikea::hashtable::owner_integration {
// Construct after capacity preparation while this owner and its backing stay
// stable. Header bytes are a local coverage oracle, never durable identity.
class damage_check {
    struct region {
        unsigned plane;
        std::span<const std::byte> storage;
        std::vector<std::byte> old;
        std::vector<std::uint8_t> covered;
    };
    const void* owner_;
    word key_;
    std::span<const std::byte> (*current_)(const void*,unsigned) noexcept;
    std::vector<region> regions_;
    std::size_t observations_ = 0;
    bool before_ok_ = true;

    region* resolve(const void* source, unsigned id) noexcept {
        if (source != owner_) return nullptr;
        const auto storage = current_(owner_,id);
        for (auto& r : regions_)
            if (r.plane == id && r.storage.data() == storage.data() &&
                r.storage.size() == storage.size()) return &r;
        return nullptr;
    }
    static bool bounded(const region& r, std::size_t offset, std::size_t size) noexcept {
        return size && offset <= r.storage.size() && size <= r.storage.size() - offset;
    }

  public:
    bool allow_write = true, allow_prepare = true;
    std::size_t capacity = 12;

    template <class Owner> damage_check(const Owner& owner, word key) : owner_(&owner), key_(key),
        current_([](const void* source, unsigned id) noexcept {
            return static_cast<const Owner*>(source)->plane_storage(plane(id));
        }) {
        for (auto id : {plane::header,plane::primary,plane::swiss_controls,plane::swiss_tuples}) {
            const auto storage = owner.plane_storage(id);
            require(storage.data() || storage.empty(), "invalid owner damage source");
            std::vector<std::byte> old;
            if (!storage.empty()) old.assign(storage.begin(),storage.end());
            regions_.push_back({unsigned(id),storage,std::move(old),std::vector<std::uint8_t>(storage.size())});
        }
    }
    template <class Owner>
    bool enter(const Owner& owner, word key, access_intent intent) const noexcept {
        return owner_ == &owner && key_ == key &&
               (intent == access_intent::read || (intent == access_intent::write && allow_write));
    }
    bool prepare(std::span<const ikea::journal::entry> writes) noexcept {
        if (!allow_prepare || writes.size() > capacity) return false;
        for (const auto& d : writes) {
            auto* r = resolve(d.source,d.plane);
            if (!r || !bounded(*r,d.offset,d.size)) return false;
        }
        return true;
    }
    template<class Owner>
    void before(const Owner& source, unsigned id, std::size_t offset, std::size_t size) noexcept {
        ++observations_;
        auto* r = resolve(&source,id);
        if (!r || !bounded(*r,offset,size)) {
            before_ok_ = false;
            return;
        }
        for (std::size_t i = offset; i < offset + size; ++i) {
            before_ok_ &= r->storage[i] == r->old[i];
            r->covered[i] = 1;
        }
    }
    void verify(bool unchanged = false) const {
        require(before_ok_, "qualified damage did not precede stores");
        for (const auto& r : regions_)
            for (std::size_t i = 0; i < r.old.size(); ++i)
                if (r.old[i] != r.storage[i])
                    require(!unchanged && r.covered[i], "uncovered or refused owner store");
        if (unchanged) require(!observations_, "unchanged operation published damage");
    }
    std::size_t observations() const noexcept { return observations_; }
    std::size_t covered_bytes() const noexcept {
        std::size_t total = 0;
        for (const auto& r : regions_)
            total += std::count(r.covered.begin(),r.covered.end(),std::uint8_t{1});
        return total;
    }
};
} // namespace ikea::hashtable::owner_integration
