#include <ikea/hashtable/descriptor.h>
#include <iostream>
using namespace ikea::hashtable;

struct refusal {
    std::errc reason;
};
struct fallible_heap {
    struct context {
        bool reject = false, reject_growth = false;
        int retains_until_failure = -1;
        unsigned tokens = 0;
    };
    struct state {
        context* source;
        bool owner_alive = true;
    };
    struct token {
        std::shared_ptr<state> retained;
        explicit token(std::shared_ptr<state> value) : retained(std::move(value)) {
            if (retained->source)
                ++retained->source->tokens;
        }
        token(const token&) = delete;
        token(token&&) noexcept = default;
        token& operator=(token&& other) noexcept {
            token old(std::move(other));
            retained.swap(old.retained);
            return *this;
        }
        ~token() {
            if (retained) {
                if (!retained->owner_alive) {
                    std::cerr << "retention released after its owner state\n";
                    std::abort();
                }
                if (retained->source)
                    --retained->source->tokens;
            }
        }
    };
    using retention = std::vector<token>;
    using trim_result = heap_storage_policy::trim_result;
    struct buffer : heap_storage_policy::buffer {
        std::shared_ptr<state> identity;
        static std::size_t admit(std::size_t n, context* c) {
            if (c && c->reject)
                throw refusal{std::errc::not_enough_memory};
            return n;
        }
        buffer() = default;
        buffer(std::size_t n, context* c = nullptr, bool zero = true, std::size_t reserve = 0)
            : heap_storage_policy::buffer(admit(n, c), nullptr, zero, reserve),
              identity(std::make_shared<state>(state{c})) {}
        buffer(buffer&&) noexcept = default;
        buffer& operator=(buffer&& other) noexcept {
            swap(other);
            return *this;
        }
        void swap(buffer& other) noexcept {
            heap_storage_policy::buffer::swap(other);
            identity.swap(other.identity);
        }
        ~buffer() {
            if (identity)
                identity->owner_alive = false;
        }
        void grow(std::size_t n, std::size_t reserve = 0, bool zero = true) {
            if (identity && identity->source && identity->source->reject_growth)
                throw refusal{std::errc::no_buffer_space};
            heap_storage_policy::buffer::grow(n, reserve, zero);
        }
        void retain(retention& holds) const {
            if (!size)
                return;
            auto* c = identity->source;
            if (c) {
                if (c->retains_until_failure == 0)
                    throw refusal{std::errc::resource_unavailable_try_again};
                if (c->retains_until_failure > 0)
                    --c->retains_until_failure;
            }
            holds.emplace_back(identity);
        }
    };
};
unsigned count(unsigned i) {
    return i == 510 ? 600 : 1 + i % 5;
}
template <unsigned V> bytes<V> payload(unsigned i, unsigned j) {
    bytes<V> v{};
    for (unsigned x = 0; x < V; ++x)
        v[x] = byte(i * 29 + j * 71 + x * 13);
    return v;
}
template <unsigned V, bool Multi> void history() {
    using D = descriptor<24, V, fallible_heap>;
    constexpr word seed = 311;
    const permutation<24, 19> mix{seed};
    fallible_heap::context context;
    // All keys initially route to one primary bucket, forcing the overflow
    // buffers as well as complete singleton/variable StreamPack allocations.
    auto saved = [&] {
        auto current = D::template explicit_encoding<5, 19>(seed, &context);
        for (unsigned i = 0; i < 512; ++i)
            for (unsigned j = 0; j < (Multi ? count(i) : 1); ++j)
                current.insert(mix.recover(0, i), payload<V>(i, j));
        current.validate();
        context.retains_until_failure = 2;
        bool capture_denied = false;
        try {
            auto failed = current.template read_native<5, 19>();
        } catch (const refusal& failure) {
            capture_denied = failure.reason == std::errc::resource_unavailable_try_again;
        }
        require(capture_denied && context.tokens == 0,
                "partial retention failure releases earlier tokens with owner alive");
        context.retains_until_failure = -1;
        auto old = current.template read_native<5, 19>();
        static_assert(sizeof(old.borrow()) == sizeof(void*));
        static_assert(!std::is_copy_constructible_v<decltype(current.read())>);
        auto reassigned = current.read();
        reassigned = current.read();
        const auto* functions = current.functions();
        context.reject = true;
        bool denied = false;
        try {
            current.template rebuild_explicit<6, 18>();
        } catch (const refusal& failure) {
            denied = failure.reason == std::errc::not_enough_memory;
        }
        require(denied && current.functions() == functions && current.size() == 512,
                "typed storage failure preserves published descriptor");
        context.reject = false;
        current.template rebuild_explicit<6, 18>();
        require(current.functions() != functions, "growth changes concrete owner interpretation");
        for (unsigned i = 0; i < 512; i += 2)
            require(current.erase(mix.recover(0, i)), "new owner erase");
        auto reader = current.read();
        for (unsigned i = 0; i < 512; ++i) {
            unsigned seen = 0;
            const bool found = reader.find(mix.recover(0, i), [&](const auto& value) {
                require(value == payload<V>(i, seen++), "rebuilt complete value");
            });
            require(found == bool(i % 2) && seen == (V && i % 2 ? (Multi ? count(i) : 1) : 0),
                    "rebuilt membership and multiplicity");
        }
        current.validate();
        auto replacement = D::template explicit_encoding<5, 19>(seed, &context);
        replacement.insert(mix.recover(0, 1023), payload<V>(1023, 0));
        auto target = replacement.template read_native<5, 19>();
        target = std::move(old);
        return target;
    }();
    const auto borrowed = saved.borrow();
    for (unsigned i = 0; i < 1024; ++i) {
        unsigned seen = 0;
        const bool found = borrowed.find(mix.recover(0, i), [&](const auto& value) {
            require(value == payload<V>(i, seen++), "old complete ordered value");
        });
        require(found == (i < 512) && seen == (V && i < 512 ? (Multi ? count(i) : 1) : 0),
                "retained old native reader after replacement and destruction");
    }
}
void mutable_batch_growth() {
    using Owner = table<24, 5, 8, 19, search_kernel::generic, fallible_heap>;
    using D = descriptor<24, 8, fallible_heap>;
    constexpr word seed = 311;
    permutation<24, 19> mix{seed};
    fallible_heap::context context;
    auto current = D::explicit_encoding<5, 19>(seed, &context);
    for (unsigned i = 0; i < Owner::N + 14; ++i)
        current.insert(mix.recover(0, i), payload<8>(i, 0));
    // The next collision replaces the full 16-slot Swiss backing. A synchronous
    // mutable binding must not retain tokens tied to buffers it can replace.
    current.with_native_mutation<5, 19>([&](auto& native, auto) {
        for (unsigned i = Owner::N + 14; i < Owner::N + 80; ++i)
            native.modify_or_insert(mix.recover(0, i), payload<8>(i, 0), [](auto&, bool present) {
                require(!present, "new batch key");
            });
    });
    require(context.tokens == 0 && current.size() == Owner::N + 80,
            "batch releases without obsolete tokens");
    for (unsigned i = 0; i < Owner::N + 80; ++i)
        require(current.read().find(mix.recover(0, i),
                                    [&](const auto& v) {
                                        require(v == payload<8>(i, 0),
                                                "complete batch growth value");
                                    }),
                "batch growth membership");
    current.validate();
}
int main() {
    try {
        mutable_batch_growth();
        history<0, false>();
        history<8, false>();
        history<64, false>();
        {
            fallible_heap::context c;
            fallible_heap::buffer owned(64, &c);
            owned.data[31] = 71;
            c.reject_growth = true;
            bool refused = false;
            try {
                owned.grow(128);
            } catch (const refusal& failure) {
                refused = failure.reason == std::errc::no_buffer_space;
            }
            require(refused && owned.size == 64 && owned.data[31] == 71,
                    "typed growth refusal preserves storage");
        }
        heap_storage_policy::buffer buffer(64);
        buffer.data[63] = 127;
        buffer.grow(192, 256);
        require(buffer.data[63] == 127 && buffer.data[191] == 0 && buffer.capacity() == 256,
                "heap growth preserves bytes and initializes new extent");
        require(!buffer.relinquish_tail(320) && buffer.size == 192,
                "typed trim refusal preserves extent");
        require(bool(buffer.relinquish_tail(64)) && buffer.size == 64 && buffer.capacity() == 256,
                "heap assigned extent differs from retained capacity");
        std::cout
            << "static storage policies, typed refusal, all surfaces, overflow and retained native readers passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
