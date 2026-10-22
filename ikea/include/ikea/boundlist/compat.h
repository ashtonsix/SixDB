#pragma once
// Transitional source compatibility for historical Boundlist consumers.
// New code links Intervals for borrowed geometry and StreamPack for value images.
#include <ikea/intervals.h>
namespace ikea::intervals { namespace author {} }
namespace ikea::streampack::detail::compact { namespace detail {} namespace author {} namespace native {} }
namespace ikea::boundlist {
using namespace ikea::intervals;
using namespace ikea::streampack::detail::compact;
namespace detail {
using namespace ikea::intervals::detail;
using namespace ikea::streampack::detail::compact::detail;
}
namespace author {
using namespace ikea::intervals::author;
using namespace ikea::streampack::detail::compact::author;
}
namespace native {
using namespace ikea::intervals::native;
using namespace ikea::streampack::detail::compact::native;
}
}
