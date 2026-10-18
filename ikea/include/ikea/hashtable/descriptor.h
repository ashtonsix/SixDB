#pragma once
#include "rebuild.h"
#include "accumulate.h"
#include "encoding-selection.h"

namespace ikea::hashtable {
// Private-owner construction/rebuild seam. The caller owns publication and
// grants; this is not a concurrent root or an MVCC implementation. Prepared
// reads retain an old backing across replacement. Mutating a retained backing
// changes its contents for every reader; a capture is not a content snapshot. The borrowed storage
// context (including a Pool) must outlive every escaped capture and final token
// release. The policy covers packed backing, not STL metadata/input allocations.
template <unsigned W, unsigned V, class Storage = heap_storage_policy> class descriptor {
    using allocation_context = typename Storage::context;
    static_assert(std::is_nothrow_move_constructible_v<typename Storage::retention>);
    static_assert(std::is_nothrow_swappable_v<typename Storage::retention>);

  public:
    using value_type = bytes<V>;
    using consumer = void (*)(void*, const value_type&);
    using visitor = void (*)(void*, word, const value_type&);
    using modifier = void (*)(void*, value_type&, bool);
    struct operations {
        const void* owner_type;
        write_strategy writes;
        bool (*find)(const void*, word, consumer, void*);
        bool (*insert)(void*, word, const value_type&);
        bool (*erase)(void*, word);
        bool (*modify)(void*, word, const value_type*, modifier, void*);
        void (*visit)(const void*, visitor, void*);
        std::size_t (*size)(const void*);
        std::size_t (*capacity)(const void*);
        std::size_t (*bytes)(const void*);
        void (*validate)(const void*);
        void (*retain_storage)(const void*, typename Storage::retention&);
        storage_usage (*storage)(const void*);
        typename Storage::trim_result (*relinquish_surplus)(void*);
        std::shared_ptr<void> (*clone)(const void*, allocation_context*);
    };
    class prepared_read {
        std::shared_ptr<const void> owner_;
        const operations* functions_;
        typename Storage::retention holds_;
        friend class descriptor;
        prepared_read(std::shared_ptr<const void> owner, const operations* functions)
            : owner_(std::move(owner)), functions_(functions) {
            functions_->retain_storage(owner_.get(), holds_);
        }

      public:
        prepared_read(const prepared_read&) = delete;
        prepared_read(prepared_read&&) noexcept = default;
        prepared_read& operator=(prepared_read&& other) noexcept {
            prepared_read incoming(std::move(other));
            owner_.swap(incoming.owner_);
            std::swap(functions_, incoming.functions_);
            using std::swap;
            swap(holds_, incoming.holds_);
            return *this; // Incoming destroys old tokens before the old owner.
        }
        template <class Consume> bool find(word key, Consume&& consume) const {
            require(!(key & ~mask<W>), "read key outside W-bit domain");
            auto adapter = [&](const value_type& value) { consume(value); };
            return functions_->find(
                owner_.get(), key,
                [](void* context, const value_type& value) {
                    (*static_cast<decltype(adapter)*>(context))(value);
                },
                std::addressof(adapter));
        }
    };
    // Capture lifetime once at the ordinary binding boundary. The borrowed
    // native view is one pointer, so a whole-batch kernel can inline Owner's
    // reads without per-point shared ownership or type-erased callbacks.
    template <class Owner> class native_read {
        std::shared_ptr<const Owner> owner_;
        typename Storage::retention holds_;
        friend class descriptor;
        explicit native_read(std::shared_ptr<const Owner> owner) : owner_(std::move(owner)) {
            owner_->retain_storage(holds_);
        }

      public:
        native_read(const native_read&) = delete;
        native_read(native_read&&) noexcept = default;
        native_read& operator=(native_read&& other) noexcept {
            native_read incoming(std::move(other));
            owner_.swap(incoming.owner_);
            using std::swap;
            swap(holds_, incoming.holds_);
            return *this;
        }
        template <staged_values Values> struct basic_view {
            const Owner* owner;
            using staged_probe = typename Owner::staged_probe;
            template <class Consume>
            [[gnu::always_inline]] bool find(word key, Consume&& consume) const {
                return owner->find(key, std::forward<Consume>(consume));
            }
            void prefetch(word key) const {
                owner->prefetch(owner->prepare(key));
            }
            template <class Emit>
            void prepare_staged(staged_probe& probe, word key, Emit&& emit) const {
                owner->template prepare_staged<Values>(probe, key, std::forward<Emit>(emit));
            }
            template <class Consume, class Emit>
            [[gnu::always_inline]] bool advance(staged_probe& probe, bool& found, Consume&& consume,
                                                Emit&& emit) const {
                return owner->template advance<Values>(probe, found, std::forward<Consume>(consume),
                                                       std::forward<Emit>(emit));
            }
        };
        using view = basic_view<staged_values::dependent>;
        template <staged_values Values = staged_values::dependent>
        basic_view<Values> borrow() const {
            return {owner_.get()};
        }
        const void* owner_identity() const {
            return owner_.get();
        }
    };

  private:
    std::shared_ptr<void> owner_;
    const operations* functions_ = nullptr;
    unsigned b_ = 0, e_ = 0;
    word seed_ = 0;
    allocation_context* memory_ = nullptr;
    template <class Owner> inline static const byte owner_tag = 0;
    template <class Owner, write_strategy Writes>
    inline static const operations implementation{
        &owner_tag<Owner>,
        Writes,
        [](const void* p, word key, consumer consume, void* context) {
            return static_cast<const Owner*>(p)->find(
                key, [&](const value_type& value) { consume(context, value); });
        },
        [](void* p, word key, const value_type& value) {
            return static_cast<Owner*>(p)->template insert<Writes>(key, value);
        },
        [](void* p, word key) { return static_cast<Owner*>(p)->erase(key); },
        [](void* p, word key, const value_type* initial, modifier update, void* context) {
            if constexpr (V) {
                auto* owner = static_cast<Owner*>(p);
                if (initial)
                    return owner->modify_or_insert(
                        key, *initial,
                        [&](value_type& value, bool present) { update(context, value, present); });
                return owner->modify(key, [&](value_type& value) { update(context, value, true); });
            } else
                return false;
        },
        [](const void* p, visitor visit, void* context) {
            static_cast<const Owner*>(p)->visit(
                [&](word key, const value_type& value) { visit(context, key, value); });
        },
        [](const void* p) { return static_cast<const Owner*>(p)->size(); },
        [](const void* p) { return static_cast<const Owner*>(p)->capacity(); },
        [](const void* p) { return static_cast<const Owner*>(p)->bytes_used(); },
        [](const void* p) { static_cast<const Owner*>(p)->validate(); },
        [](const void* p, typename Storage::retention& holds) {
            static_cast<const Owner*>(p)->retain_storage(holds);
        },
        [](const void* p) { return static_cast<const Owner*>(p)->storage(); },
        [](void* p) { return static_cast<Owner*>(p)->relinquish_surplus(); },
        [](const void* p, allocation_context* memory) -> std::shared_ptr<void> {
            return std::make_shared<Owner>(static_cast<const Owner*>(p)->clone(memory));
        }};
    void replace_with(descriptor replacement) {
        // All fallible allocation, insertion and validation precede root swap.
        functions_->visit(
            owner_.get(),
            [](void* context, word key, const value_type& value) {
                static_cast<descriptor*>(context)->insert(key, value);
            },
            &replacement);
        replacement.validate();
        *this = std::move(replacement);
    }

  public:
    descriptor() = delete;
    descriptor(const descriptor&) = delete;
    descriptor(descriptor&&) = default;
    descriptor& operator=(descriptor&&) = default;
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic>
    static descriptor explicit_encoding(word seed, allocation_context* memory = nullptr,
                                        write_strategy writes = write_strategy::mixed) {
        using Owner = table<W, b, V, E, Kernel, Storage>;
        const auto* functions = writes == write_strategy::mixed
                                    ? &implementation<Owner, write_strategy::mixed>
                                    : &implementation<Owner, write_strategy::existing>;
        return descriptor(std::make_shared<Owner>(seed, memory), functions, b, E, seed & mask<W>,
                          memory);
    }
    template <unsigned b>
    static descriptor measured(word seed, std::string_view cpu, std::string_view isa,
                               allocation_context* memory = nullptr,
                               write_strategy writes = write_strategy::mixed) {
        return bind_measured_encoding<W - b>(
            V, cpu, isa, [&](auto encoding, auto kernel, encoding_choice) {
                return explicit_encoding<b, decltype(encoding)::value, decltype(kernel)::value>(
                    seed, memory, writes);
            });
    }
    template <unsigned b> void rebuild_measured(std::string_view cpu, std::string_view isa) {
        replace_with(measured<b>(seed_, cpu, isa, memory_, functions_->writes));
    }
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic>
    void rebuild_explicit() {
        replace_with(explicit_encoding<b, E, Kernel>(seed_, memory_, functions_->writes));
    }
    /// Same-seed doubling from the captured concrete specialization. Retain E
    /// and its reader when the new geometry permits; otherwise use exact generic
    /// encoding. Explicit NextE/NextKernel can select a measured destination.
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic,
              unsigned NextE =
                  ((W - b - 1) > 9 && E <= geometry::at(W, b + 1).maximum_encoding ? E : W - b - 1),
              search_kernel NextKernel =
                  (NextE == E && geometry::at(W, b).tiles == geometry::at(W, b + 1).tiles &&
                           geometry::at(W, b).bytes == geometry::at(W, b + 1).bytes
                       ? Kernel
                       : search_kernel::generic)>
    void grow() {
        static_assert(b < W);
        using Source = table<W, b, V, E, Kernel, Storage>;
        using Target = table<W, b + 1, V, NextE, NextKernel, Storage>;
        require(functions_->owner_type == &owner_tag<Source>,
                "growth source specialization differs");
        auto next = hashtable::grow<Target>(*static_cast<const Source*>(owner_.get()), memory_);
        const auto* functions = functions_->writes == write_strategy::mixed
                                    ? &implementation<Target, write_strategy::mixed>
                                    : &implementation<Target, write_strategy::existing>;
        descriptor replacement(std::shared_ptr<Target>(std::move(next)), functions, b + 1, NextE,
                               seed_, memory_);
        *this = std::move(replacement);
    }
    /// Explicit caller-owned copy; no copying is hidden in accumulation.
    descriptor clone(allocation_context* memory) const {
        return descriptor(functions_->clone(owner_.get(), memory), functions_, b_, e_, seed_,
                          memory);
    }
    descriptor clone() const {
        return clone(memory_);
    }
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic>
    void reserve_overflow(std::size_t final_live_spill_bound) {
        using Owner = table<W, b, V, E, Kernel, Storage>;
        require(functions_->owner_type == &owner_tag<Owner>,
                "overflow reservation specialization differs");
        hashtable::reserve_overflow(*static_cast<Owner*>(owner_.get()), final_live_spill_bound,
                                    memory_);
    }
    /// Destructively accumulate into exclusive backing; discard this descriptor
    /// on failure/exception. Existing captures observe writes to that backing.
    /// Source order is the order of these synchronous single-source calls.
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic,
              unsigned SourceB = b, unsigned SourceE = E, search_kernel SourceKernel = Kernel,
              class Reduce>
    [[nodiscard]] accumulation_status accumulate(const descriptor& source, Reduce&& reduce) {
        using D = table<W, b, V, E, Kernel, Storage>;
        using S = table<W, SourceB, V, SourceE, SourceKernel, Storage>;
        require(functions_->owner_type == &owner_tag<D> &&
                    source.functions_->owner_type == &owner_tag<S>,
                "accumulation specialization differs");
        return hashtable::accumulate(*static_cast<D*>(owner_.get()),
                                     *static_cast<const S*>(source.owner_.get()),
                                     std::forward<Reduce>(reduce));
    }
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic,
              unsigned SourceB = b, unsigned SourceE = E, search_kernel SourceKernel = Kernel>
    [[nodiscard]] accumulation_status accumulate(const descriptor& source)
        requires(V == 0)
    {
        return accumulate<b, E, Kernel, SourceB, SourceE, SourceKernel>(
            source, [](auto a, auto) { return a; });
    }
    write_strategy writes() const {
        return functions_->writes;
    }
    prepared_read read() const {
        return {owner_, functions_};
    }
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic>
    auto read_native() const {
        using Owner = table<W, b, V, E, Kernel, Storage>;
        require(functions_->owner_type == &owner_tag<Owner>,
                "native reader geometry differs from captured owner");
        return native_read<Owner>(std::static_pointer_cast<const Owner>(owner_));
    }
    /// Bind a complete synchronous mutable batch once. The callback receives the
    /// concrete owner and a compile-time writer strategy; it must not replace
    /// this descriptor, move the owner, reenter the binding or escape references.
    template <unsigned b, unsigned E = W - b, search_kernel Kernel = search_kernel::generic,
              class Bind>
    void with_native_mutation(Bind&& bind) {
        using Owner = table<W, b, V, E, Kernel, Storage>;
        require(functions_->owner_type == &owner_tag<Owner>,
                "native mutation specialization differs");
        auto* owner = static_cast<Owner*>(owner_.get());
        if (functions_->writes == write_strategy::mixed)
            std::invoke(std::forward<Bind>(bind), *owner,
                        std::integral_constant<write_strategy, write_strategy::mixed>{});
        else
            std::invoke(std::forward<Bind>(bind), *owner,
                        std::integral_constant<write_strategy, write_strategy::existing>{});
    }
    template <class Update>
    bool modify(word key, Update&& update)
        requires(V > 0)
    {
        auto adapter = [&](value_type& value, bool) {
            std::invoke(std::forward<Update>(update), value);
        };
        return functions_->modify(
            owner_.get(), key, nullptr,
            [](void* context, value_type& value, bool present) {
                (*static_cast<decltype(adapter)*>(context))(value, present);
            },
            std::addressof(adapter));
    }
    template <class Update>
    bool modify_or_insert(word key, const value_type& initial, Update&& update)
        requires(V > 0)
    {
        auto adapter = [&](value_type& value, bool present) {
            std::invoke(std::forward<Update>(update), value, present);
        };
        return functions_->modify(
            owner_.get(), key, &initial,
            [](void* context, value_type& value, bool present) {
                (*static_cast<decltype(adapter)*>(context))(value, present);
            },
            std::addressof(adapter));
    }
    bool insert(word key, const value_type& value = {}) {
        return functions_->insert(owner_.get(), key, value);
    }
    bool erase(word key) {
        require(!(key & ~mask<W>), "erase key outside W-bit domain");
        return functions_->erase(owner_.get(), key);
    }
    std::size_t size() const {
        return functions_->size(owner_.get());
    }
    std::size_t capacity() const {
        return functions_->capacity(owner_.get());
    }
    std::size_t bytes_used() const {
        return functions_->bytes(owner_.get());
    }
    storage_usage storage() const {
        return functions_->storage(owner_.get());
    }
    [[nodiscard]] typename Storage::trim_result relinquish_surplus() {
        return functions_->relinquish_surplus(owner_.get());
    }
    void validate() const {
        functions_->validate(owner_.get());
    }
    unsigned bucket_bits() const {
        return b_;
    }
    unsigned encoding() const {
        return e_;
    }
    const operations* functions() const {
        return functions_;
    }
    bool needs_growth(unsigned permille = 850) const {
        require(permille > 0 && permille <= 1000, "growth occupancy");
        return __uint128_t(size()) * 1000 > __uint128_t(capacity()) * permille;
    }

  private:
    descriptor(std::shared_ptr<void> owner, const operations* functions, unsigned b, unsigned e,
               word seed, allocation_context* memory)
        : owner_(std::move(owner)), functions_(functions), b_(b), e_(e), seed_(seed),
          memory_(memory) {}
};
} // namespace ikea::hashtable
