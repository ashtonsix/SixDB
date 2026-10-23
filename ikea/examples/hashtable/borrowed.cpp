#include <ikea/hashtable.h>
#include <array>
#include <cstring>
#include <iostream>

namespace ht = ikea::hashtable;
using Requests = std::array<ht::word, 1031>;

// Borrowed bytes need not be aligned or contain a C++ integer object.
ht::word field(const std::byte* p) {
    ht::word value;
    std::memcpy(&value, p, sizeof(value));
    return value;
}
template <ht::lookup_hint Hint, class View>
ht::word read_range(View view, const Requests& requests, std::size_t first, std::size_t count) {
    struct Pending { const std::byte* value; };
    std::array<Pending, 16> pending;
    ht::word sum = 0;
    const auto consume = [&](const Pending& item) {
        sum += field(item.value) + field(item.value + 56);
    };
    for (std::size_t at = 0; at < count; ++at) {
        const auto index = first + at;
        if (index + 32 < requests.size()) view.prefetch(requests[index + 32]);
        auto& item = pending[at % pending.size()];
        if (at >= pending.size()) consume(item);
        item.value = view.template get_borrowed<Hint>(requests[index]).address;
        // Every returned address has 64 readable bytes, including absence.
        __builtin_prefetch(item.value, 0, 3);
        __builtin_prefetch(item.value + 63, 0, 3);
    }
    for (std::size_t at = count > pending.size() ? count - pending.size() : 0; at < count; ++at)
        consume(pending[at % pending.size()]);
    return sum;
}

int main() {
    auto owner = ht::erased_table<24, 64>::explicit_encoding<10>(713);
    for (ht::word key = 0; key < 64; ++key) {
        ht::bytes<64> value{};
        const ht::word first = key + 1, last = first * 3;
        std::memcpy(value.data(), &first, 8);
        std::memcpy(value.data() + 56, &last, 8);
        owner.insert(key, value);
    }
    auto held = owner.read_native<10>();
    auto view = held.borrow();
    // Capture retention keeps the old image alive after descriptor replacement.
    // Reads stay coherent: this example never mutates that captured image.
    owner.rebuild_explicit<11>();
    owner.erase(7);
    Requests requests;
    ht::word expected = 0;
    for (unsigned i = 0; i < requests.size(); ++i) {
        requests[i] = i % 97;
        if (requests[i] < 64) expected += (requests[i] + 1) * 4;
    }
    ht::lookup_selector selector;
    ht::word actual = 0;
    selector.run(requests.size(), [&](auto hint, std::size_t first, std::size_t count) {
        actual += read_range<decltype(hint)::value>(view, requests, first, count);
    });
    ht::require(actual == expected, "borrowed projected consumption, zero misses and partial tail");
    std::cout << "borrowed values: retained owner, deferred fields, automatic selection passed\n";
}
