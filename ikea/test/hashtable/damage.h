#pragma once
#include <ikea/hashtable/table.h>
#include <algorithm>
#include <string_view>
#include <vector>

namespace ikea::hashtable::owner_integration {
// Construct after capacity preparation, while this private owner and its sources
// stay stable. Header bytes are a local coverage oracle, never durable identity.
class damage_check {
    struct region {
        mutation_source source;
        std::vector<byte> old, covered;
    };
    const void* owner_;
    word key_;
    std::vector<region> regions_;
    std::size_t observations_ = 0;
    bool before_ok_ = true;

    static owner_part part(std::string_view name) {
        if (name == "primary")
            return owner_part::primary;
        if (name == "swiss_controls")
            return owner_part::swiss_controls;
        if (name == "swiss_tuples")
            return owner_part::swiss_tuples;
        if (name == "singleton_bags")
            return owner_part::singleton_bags;
        if (name == "small_variable_bags")
            return owner_part::small_variable_bags;
        if (name == "large_variable_bags")
            return owner_part::large_variable_bags;
        throw std::runtime_error("unknown owner damage part");
    }

    region* resolve(mutation_source source) noexcept {
        for (auto& r : regions_)
            if (r.source.owner == source.owner && r.source.part == source.part &&
                r.source.base == source.base && r.source.extent == source.extent)
                return &r;
        return nullptr;
    }

    static bool bounded(mutation_source source, ikea::byte_write span) noexcept {
        return !span.plane && span.size && span.offset <= source.extent &&
               span.size <= source.extent - span.offset;
    }

  public:
    bool allow_write = true, allow_prepare = true;
    std::size_t capacity = 12;

    template <class Owner> damage_check(const Owner& owner, word key) : owner_(&owner), key_(key) {
        auto add = [&](owner_part id, const byte* base, std::size_t extent) {
            require(base || !extent, "invalid owner damage source");
            region r{
                {owner_, id, base, extent}, std::vector<byte>(extent), std::vector<byte>(extent)};
            if (extent)
                std::copy_n(base, extent, r.old.data());
            regions_.push_back(std::move(r));
        };
        add(owner_part::header, reinterpret_cast<const byte*>(&owner), sizeof(owner));
        owner.allocations([&](const char* name, const byte* base, std::size_t extent) {
            add(part(name), base, extent);
        });
    }

    template <class Owner>
    bool enter(const Owner& owner, word key, access_intent intent) const noexcept {
        return owner_ == &owner && key_ == key &&
               (intent == access_intent::read || (intent == access_intent::write && allow_write));
    }

    bool prepare(std::span<const mutation_damage> writes) noexcept {
        if (!allow_prepare || writes.size() > capacity)
            return false;
        for (const auto& d : writes)
            if (!resolve(d.source) || !bounded(d.source, d.span))
                return false;
        return true;
    }

    void before(mutation_source source, ikea::byte_write span) noexcept {
        ++observations_;
        auto* r = resolve(source);
        if (!r || !bounded(source, span)) {
            before_ok_ = false;
            return;
        }
        for (std::size_t i = span.offset; i < span.offset + span.size; ++i) {
            before_ok_ &= source.base[i] == r->old[i];
            r->covered[i] = 1;
        }
    }

    void verify(bool unchanged = false) const {
        require(before_ok_, "qualified damage did not precede stores");
        for (const auto& r : regions_)
            for (std::size_t i = 0; i < r.old.size(); ++i)
                if (r.old[i] != r.source.base[i])
                    require(!unchanged && r.covered[i], "uncovered or refused owner store");
        if (unchanged)
            require(!observations_, "unchanged operation published damage");
    }

    std::size_t observations() const noexcept {
        return observations_;
    }
    std::size_t covered_bytes() const noexcept {
        std::size_t total = 0;
        for (const auto& r : regions_)
            total += std::count(r.covered.begin(), r.covered.end(), byte{1});
        return total;
    }
};
} // namespace ikea::hashtable::owner_integration
