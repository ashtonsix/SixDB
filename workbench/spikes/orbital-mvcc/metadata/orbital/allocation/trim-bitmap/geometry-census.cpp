#include "finite.h"
#include <cstdio>
#include <cstdlib>
using namespace orbital::allocation::detail;
int main() {
  for (auto palette : {Palette::a, Palette::b})
    for (auto width : {64, 256, 1024}) {
      auto result = FiniteGroup::create({palette, static_cast<std::uint16_t>(width)});
      if (!result || !(*result)->prepare()) std::abort();
      auto& group = **result;
      std::printf("%s,%d,%zu,%zu,%zu\n", palette == Palette::a ? "A" : "B", width,
          group.payload_pages(), group.live_words(), group.control_bytes());
    }
}
