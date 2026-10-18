#include <ikea/hashtable.h>
#include <array>
#include <cstring>
#include <iostream>

namespace ht = ikea::hashtable;

int main() {
    ht::table<24, 10, 0> keys(713);
    if (!keys.insert(42) || !keys.find(42, [](auto) {}))
        return 1;

    ht::table<24, 10, 8> values(713);
    ht::bytes<8> value{};
    const std::uint64_t answer = 99;
    std::memcpy(value.data(), &answer, sizeof(answer));
    values.insert(42, value);
    std::uint64_t result = 0;
    if (!values.find(
            42, [&](const auto& bytes) { std::memcpy(&result, bytes.data(), sizeof(result)); }) ||
        result != answer)
        return 1;

    // The callback is inlinable; existing RMW writes only the value fields.
    if (!values.modify(42, [](auto& bytes) {
            std::uint64_t count;
            std::memcpy(&count, bytes.data(), sizeof(count));
            ++count;
            std::memcpy(bytes.data(), &count, sizeof(count));
        }))
        return 1;

    using Larger = ht::table<24, 11, 8, 14>;
    auto grown = ht::grow<Larger>(values);
    if (!grown->find(
            42, [&](const auto& bytes) { std::memcpy(&result, bytes.data(), sizeof(result)); }) ||
        result != answer + 1)
        return 1;
    grown->erase(42);
    if (!values.find(42, [](auto) {}))
        return 1;
    std::cout << "sets, maps and independent same-seed growth passed\n";
}
