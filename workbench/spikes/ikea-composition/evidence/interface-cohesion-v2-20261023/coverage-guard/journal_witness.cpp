#include <ikea/effects.h>
#include <array>
#include <cassert>
#include <type_traits>
struct source { int value; };
struct other_source { int value; };
static_assert(ikea::effects<ikea::journal, source>);
static_assert(ikea::effects<ikea::journal, other_source>);
static_assert(std::is_aggregate_v<ikea::journal>);
int main() {
    const source a{42}, b{42};
    const other_source c{42};
    std::array<ikea::journal::entry, 10> storage{};
    ikea::journal journal{storage};
    journal.before(a, 1, 16, 8);
    journal.before(a, 1, 24, 8);
    journal.before(a, 1, 8, 8);
    journal.before(a, 2, 8, 8);
    journal.before(b, 2, 8, 8);
    journal.before(a, 2, 24, 8);
    journal.before(a, 2, 32, 8);
    journal.before(a, 2, 0, 8);
    journal.before(a, 2, 16, 8);
    journal.before(c, 2, 16, 8);
    const std::array<ikea::journal::entry, 7> expected{{
        {&a,1,8,24}, {&a,2,8,8}, {&b,2,8,8}, {&a,2,24,16},
        {&a,2,0,8}, {&a,2,16,8}, {&c,2,16,8}}};
    assert(journal.used == expected.size());
    assert(journal.remaining() == storage.size() - expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto actual = journal.entries()[i];
        assert(actual.source == expected[i].source && actual.plane == expected[i].plane);
        assert(actual.offset == expected[i].offset && actual.size == expected[i].size);
    }
}
