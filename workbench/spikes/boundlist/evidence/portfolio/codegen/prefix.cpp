// Scheduling diagnostic only; live complete consumers decide adoption.
#include <ikea/boundlist/detail/prefix.h>
extern "C" unsigned boundlist_owner_u16_narrow(const std::uint16_t* sizes, unsigned point) {
    return ikea::boundlist::detail::owner16_native(sizes, point, 65535);
}
