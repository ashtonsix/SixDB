#include <ikea/intervals.h>
#include <ikea/effects.h>
#include <array>
#include <cassert>
int main() {
    namespace in=ikea::intervals;
    std::array<std::uint8_t,64> widths{};
    std::array<std::uint32_t,1> checkpoints{};
    unsigned assigned=0;std::uint32_t extent=0;
    const in::size_editor<64,std::uint8_t> owner{widths,checkpoints,assigned,extent,0,4096,255};
    std::array<ikea::owner_write,32> records;
    ikea::source_write_journal effects{records};
    assert(owner.append(3,effects));assert(owner.append(0,effects));assert(owner.append(5,effects));
    assert(owner.validate());
    const auto source=owner.source();
    assert(in::reverse(source,3)==2); // Empty rank1 retains its identity.
    const std::array<std::byte,1> eligible{std::byte{0b011}};
    assert(in::reverse(source,3,in::rank_selection{eligible})==3); // Owner excluded.
    unsigned visited=0;
    assert(in::range(source,0,3,[&](unsigned,auto) {++visited;}));
    assert(visited==3);
    const std::array<std::uint8_t,3> ends_minus_one{0,3,255};
    const in::run_end_source runs{ends_minus_one.data(),3};
    assert(in::reverse(runs,255)==2 && runs.extent()==256);
}
