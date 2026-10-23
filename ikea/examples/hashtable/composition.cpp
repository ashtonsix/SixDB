#include <ikea/hashtable.h>
#include <array>
#include <iostream>

namespace ht = ikea::hashtable;

int main() {
    using Descriptor = ht::erased_table<24, 8>;
    auto values = Descriptor::explicit_encoding<10, 14>(713);
    ht::bytes<8> payload{};
    payload[0] = std::byte{7};
    for (std::uint64_t key : {11, 22, 33})
        values.insert(key, payload);

    // Capture ownership once, then pass one borrowed pointer into a native loop.
    auto capture = values.read_native<10, 14>();
    auto native = capture.borrow();
    const std::array<std::uint64_t, 4> requests{11, 44, 22, 33};
    std::uint64_t sum = 0;
    for (const auto key : requests)
        native.find(key, [&](const auto& value) { sum += std::to_integer<unsigned>(value[0]); });
    if (sum != 21)
        return 1;

    // The descriptor binds a concrete mutable batch once. The constant writer
    // keeps an explicit existing/mixed selection for any inserts in this batch.
    values.with_native_mutation<10, 14>([&](auto& owner, auto writer) {
        for (const auto key : {11, 22, 33})
            owner.modify(key, [](auto& value) { value[0] = std::byte(std::to_integer<unsigned>(value[0]) + 1); });
        owner.template insert<decltype(writer)::value>(44, payload);
    });
    if (!native.find(11, [](const auto& value) {
            if (value[0] != std::byte{8})
                throw std::runtime_error("captures retain backing, not old contents");
        }))
        return 1;

    // Rebuild finishes before the descriptor changes its owner and functions.
    const auto* old_functions = values.functions();
    values.grow<10, 14>();
    if (values.functions() == old_functions)
        return 1;
    values.erase(11);
    if (values.read().find(11, [](auto) {}) || !native.find(11, [](auto) {}))
        return 1;
    std::cout << "native batch, descriptor rebinding and retained old reader passed\n";
}
