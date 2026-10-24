#include <ikea/bec/common.h>

namespace ikea::bec {
std::string_view describe(error value) noexcept {
    switch (value) {
    case error::population:
        return "population is outside the variant's 0..positions or differs from input";
    case error::truncated:
        return "body ends inside an encoded field";
    case error::split:
        return "bisection population is out of range";
    case error::rank:
        return "enumerative byte rank is out of range";
    case error::padding:
        return "nonzero final padding bits";
    case error::trailing:
        return "length is not the exact headless body length";
    case error::capacity:
        return "destination or readable extent is too small";
    case error::effects:
        return "effect journal needs one available entry";
    case error::overlap:
        return "destination overlaps command or effect storage";
    case error::length:
        return "block length is out of range or differs from its stream";
    }
    return "unknown Bec error";
}
} // namespace ikea::bec
