#pragma once
#include <ikea/effects.h>
namespace ikea::seriespack {
using ikea::byte_write;
using ikea::no_coverage;
using ikea::write_journal;
namespace composition {
using ikea::owner_write;
using write_journal = ikea::source_write_journal;
}
} // namespace ikea::seriespack
