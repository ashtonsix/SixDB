// Serialized owner integration checks, not a Loom/Engine/Orbital protocol.
#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/composition.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <source_location>
#include <tuple>
#include <type_traits>

namespace tp = ikea::tuplepack;
namespace cp = tp::composition;
using tp::byte;
namespace {
std::size_t checks = 0;
void check(bool valid, const char* expression,
           std::source_location where = std::source_location::current()) {
    ++checks;
    if (valid)
        return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), expression);
    std::abort();
}
#define CHECK(...) check(bool(__VA_ARGS__), #__VA_ARGS__)
template <class T> T required(std::expected<T, tp::error> value) {
    CHECK(value);
    return std::move(*value);
}

constexpr std::size_t rows = 80, first = 61, length = 12, chunk = 4;
constexpr std::size_t block_rows = 8, blocks = rows / block_rows;
constexpr std::size_t primary_offset = 1, primary_stride = 8;
constexpr std::size_t replacement_offset = 2, replacement_stride = 5;
constexpr std::uint64_t primary_id = 101, replacement_id = 202, mapping_generation = 7;
constexpr std::uint64_t encoding_edition = 19;
enum class policy { exact, conservative, bypass };

byte old_a(std::size_t row) {
    return byte(row % 4);
}
byte old_b(std::size_t row) {
    return byte((row * 3 + 2) % 4);
}
byte unchanged_c(std::size_t row) {
    return byte(64 + row % 4);
}
byte new_a(std::size_t row) {
    return old_a(row) == 3 ? 0 : byte(8 + old_a(row));
}
byte new_b(std::size_t row) {
    return byte(12 + old_b(row));
}
bool selected(std::size_t row) {
    return row >= first && row < first + length && row % 3 != 2;
}
bool completed(std::size_t row, std::size_t progress) {
    return selected(row) && row < first + progress;
}
byte seed(std::size_t offset, unsigned salt) {
    return byte(offset * 37 + salt);
}
std::uint16_t token(byte value) {
    return std::uint16_t(1u << (value % 16));
}
std::uint16_t signature(byte a, byte b, byte c) {
    return token(a) | token(b) | token(c);
}

tp::layout primary_layout() {
    const std::array codes{tp::code{0, 0, 4}, tp::code{0, 4, 4}, tp::code{1, 0, 8}};
    return required(tp::layout::make(2, codes));
}
tp::layout replacement_layout() {
    const std::array codes{tp::code{0, 2, 4}};
    return required(tp::layout::make(1, codes));
}
struct storage {
    std::array<byte, primary_offset + rows * primary_stride> primary;
    std::array<byte, replacement_offset + rows * replacement_stride> replacement;
    storage() {
        for (std::size_t i = 0; i < primary.size(); ++i)
            primary[i] = seed(i, 17);
        for (std::size_t i = 0; i < replacement.size(); ++i)
            replacement[i] = seed(i, 93);
        for (std::size_t row = 0; row < rows; ++row) {
            primary[primary_offset + row * primary_stride] = old_a(row) | byte(old_b(row) << 4);
            primary[primary_offset + row * primary_stride + 1] = unchanged_c(row);
            auto& b = replacement[replacement_offset + row * replacement_stride];
            b = byte((b & 0xc3) | (old_b(row) << 2));
        }
    }
};

// Fixture formulas check current bytes and untouched gaps; no retained data
// baseline, private data candidate or beforeimage is required by this adapter.
void check_storage(const storage& data, bool substituted, std::size_t progress) {
    for (std::size_t i = 0; i < data.primary.size(); ++i) {
        byte expected = seed(i, 17);
        if (i >= primary_offset) {
            const auto row = (i - primary_offset) / primary_stride;
            const auto position = (i - primary_offset) % primary_stride;
            if (row < rows && position == 0) {
                const byte a = completed(row, progress) ? new_a(row) : old_a(row);
                const byte b = completed(row, progress) && !substituted ? new_b(row) : old_b(row);
                expected = a | byte(b << 4);
            } else if (row < rows && position == 1)
                expected = unchanged_c(row);
        }
        CHECK(data.primary[i] == expected);
    }
    for (std::size_t i = 0; i < data.replacement.size(); ++i) {
        byte expected = seed(i, 93);
        if (i >= replacement_offset) {
            const auto row = (i - replacement_offset) / replacement_stride;
            if (row < rows && (i - replacement_offset) % replacement_stride == 0) {
                const byte b = completed(row, progress) && substituted ? new_b(row) : old_b(row);
                expected = byte((expected & 0xc3) | (b << 2));
            }
        }
        CHECK(data.replacement[i] == expected);
    }
}

struct truth_bounds {
    bool certain = false, possible = false;
    bool operator==(const truth_bounds&) const = default;
};
struct metadata {
    std::array<std::uint16_t, rows> masks{};
    std::array<truth_bounds, rows> a_zero{};
    std::array<bool, rows> row_bypass{};
    std::array<std::uint16_t, blocks> block_masks{};
    std::array<bool, blocks> block_bypass{};
    std::uint16_t root_mask = 0;
    bool root_bypass = false;
    std::uint64_t edition = encoding_edition;
    bool operator==(const metadata&) const = default;
};
void rebuild_ancestors(metadata& value) {
    value.block_masks.fill(0);
    value.root_mask = 0;
    for (std::size_t row = 0; row < rows; ++row)
        value.block_masks[row / block_rows] |= value.masks[row];
    for (auto mask : value.block_masks)
        value.root_mask |= mask;
}
metadata initial_metadata() {
    metadata value;
    for (std::size_t row = 0; row < rows; ++row) {
        value.masks[row] = signature(old_a(row), old_b(row), unchanged_c(row));
        value.a_zero[row] = {old_a(row) == 0, old_a(row) == 0};
    }
    rebuild_ancestors(value);
    return value;
}
struct resolved_write {
    std::uint64_t partition, generation;
    ikea::byte_write bytes;
};
constexpr std::size_t journal_capacity = rows * 4;

// One serialized owner boundary models exclusion and coordinated publication.
// Version retention (including UFFD COW), durability and concurrent scheduling
// are intentionally outside this executable's claims.
struct owner {
    std::unique_ptr<storage> readable = std::make_unique<storage>();
    metadata published = initial_metadata();
    std::array<resolved_write, journal_capacity> writes{};
    std::size_t used = 0;
    std::uint64_t revision = 1;
    const bool substituted;
    explicit owner(bool use_replacement) : substituted(use_replacement) {}
    bool can_read() const noexcept {
        return bool(readable);
    }
    bool may_contain(std::size_t row, std::uint16_t wanted) const {
        CHECK(readable && row < rows);
        if (!published.root_bypass && (published.root_mask & wanted) != wanted)
            return false;
        const auto block = row / block_rows;
        if (!published.block_bypass[block] && (published.block_masks[block] & wanted) != wanted)
            return false;
        return published.row_bypass[row] || (published.masks[row] & wanted) == wanted;
    }
    void return_clean(std::unique_ptr<storage> residency) {
        CHECK(!readable && residency && used == 0 && revision == 1);
        check_storage(*residency, substituted, 0);
        CHECK(published == initial_metadata());
        readable = std::move(residency);
    }
    void publish(std::unique_ptr<storage> residency, metadata staged,
                 std::span<const resolved_write> effects) {
        CHECK(!readable && residency && staged.edition == encoding_edition);
        CHECK(effects.size() <= writes.size());
        rebuild_ancestors(staged);
        // Hooks only wrote retained private metadata. Its application and these
        // resolved data effects belong to this owner publication, outside Ikea.
        // No persistent summary store is smuggled through data-journal capacity.
        published = staged;
        used = effects.size();
        std::copy(effects.begin(), effects.end(), writes.begin());
        ++revision;
        readable = std::move(residency);
    }
};

struct maintenance_state {
    metadata staged;
    std::array<unsigned, rows> visits{};
    std::array<bool, rows> changed{};
    unsigned projection_reads = 0;
};
using observed_values = std::tuple<std::uint64_t, std::uint64_t>;
template <policy Policy> struct signature_law {
    static constexpr bool needs_before = false, needs_after = Policy != policy::bypass;
    maintenance_state* state;
    const std::uint64_t edition;
    void mark(std::size_t row) noexcept {
        CHECK(edition == state->staged.edition && selected(row));
        CHECK(state->visits[row]++ == 0);
        state->changed[row] = true;
    }
    void observe(std::size_t row, const observed_values& values) noexcept {
        mark(row);
        // O differs from both mutation maps: (C, hole, A), then (hole, B, B).
        const auto [ac, b_packet] = values;
        const byte a = byte(ac >> 16), b = byte(b_packet >> 8), c = byte(ac);
        CHECK(a == new_a(row) && b == new_b(row) && c == unchanged_c(row));
        CHECK(byte(ac >> 8) == 0 && byte(b_packet) == 0 && byte(b_packet >> 16) == b);
        CHECK((ac >> 24) == 0 && (b_packet >> 24) == 0);
        const auto fresh = signature(a, b, c);
        // C retains the old A token even when A changes. Clearing old A's hash
        // contribution would create a false negative for this untouched field.
        CHECK((fresh & token(old_a(row))) != 0);
        if constexpr (Policy == policy::exact) {
            state->staged.masks[row] = fresh;
            state->staged.a_zero[row] = {a == 0, a == 0};
        } else {
            state->staged.masks[row] |= fresh;
            state->staged.a_zero[row] = {false, true};
        }
    }
    void observe(std::size_t row) noexcept {
        static_assert(Policy == policy::bypass);
        mark(row);
        state->staged.row_bypass[row] = true;
        state->staged.block_bypass[row / block_rows] = true;
        state->staged.root_bypass = true;
        state->staged.a_zero[row] = {false, true};
    }
};
using read8 = tp::read_operation<8, byte>;
using projection_type = cp::projection<read8, read8>;
struct observed_projection {
    projection_type bound;
    maintenance_state* state;
    std::size_t size() const noexcept {
        return bound.size();
    }
    auto get_unchecked(std::size_t row) const {
        ++state->projection_reads;
        return bound.get_unchecked(row);
    }
};

struct reply_counts {
    unsigned acquired = 0, released = 0;
};
struct reply_lease {
    reply_counts* counts;
    explicit reply_lease(reply_counts& value) : counts(&value) {
        ++counts->acquired;
    }
    ~reply_lease() {
        ++counts->released;
    }
};
struct completion {
    std::uint64_t request_generation, mapping;
    std::unique_ptr<reply_lease> residency;
};
enum class phase { running, suspended, owner_resolution, complete, released };
enum class cancellation { clean_released, dirty_retained };

template <policy Policy> class retained_work {
    using leaf = tp::mutation_operation<8>;
    using group_type = cp::mutation_group<leaf, leaf>;
    owner* target_;
    std::unique_ptr<storage> residency_;
    tp::layout primary_format_ = primary_layout(), replacement_format_ = replacement_layout();
    tp::view primary_view_, replacement_view_;
    tp::writer<8> a_plan_, original_b_plan_, replacement_b_plan_;
    leaf a_, original_b_, replacement_b_;
    group_type operation_;
    tp::reader<8> ac_plan_, original_b_read_plan_, replacement_b_read_plan_;
    read8 ac_, original_b_read_, replacement_b_read_;
    maintenance_state state_;
    observed_projection projection_;
    signature_law<Policy> law_;
    tp::observation<observed_projection, signature_law<Policy>> maintenance_;
    std::array<typename group_type::input_type, length> input_{};
    std::array<std::uint64_t, 2> selected_words_{};
    std::array<ikea::owner_write, journal_capacity> records_{};
    ikea::source_write_journal effects_{records_};
    std::unique_ptr<reply_lease> prefetched_;
    std::size_t next_ = 0;
    std::uint64_t request_generation_ = 11;
    phase phase_ = phase::running;

    static auto reader(const tp::layout& format, std::span<const byte> map) {
        // Empty projection for invalidation is intentional: invocation is
        // independent of both old-value and new-value demand.
        if constexpr (Policy == policy::bypass)
            return required(tp::reader<8>::make(format, {}));
        else
            return required(tp::reader<8>::make(format, map));
    }
    auto selection() const {
        return tp::selection::bits(0, selected_words_);
    }

  public:
    explicit retained_work(owner& target)
        : target_(&target), residency_(std::move(target.readable)),
          primary_view_(required(tp::view::bind(primary_format_, residency_->primary, rows,
                                                primary_stride, primary_offset))),
          replacement_view_(required(tp::view::bind(replacement_format_, residency_->replacement,
                                                    rows, replacement_stride, replacement_offset))),
          a_plan_(required(tp::writer<8>::make(primary_format_, std::array<byte, 2>{tp::hole, 0}))),
          original_b_plan_(required(tp::writer<8>::make(primary_format_, std::array<byte, 1>{1}))),
          replacement_b_plan_(
              required(tp::writer<8>::make(replacement_format_, std::array<byte, 1>{0}))),
          a_(required(tp::bind_writer(a_plan_, primary_view_))),
          original_b_(required(tp::bind_writer(original_b_plan_, primary_view_))),
          replacement_b_(required(tp::bind_writer(replacement_b_plan_, replacement_view_))),
          // Rebuild the parent using the substituted child. The retired B
          // binding stays alive but must contribute neither reads nor effects.
          operation_(
              required(cp::bind_group(a_, target.substituted ? replacement_b_ : original_b_))),
          ac_plan_(reader(primary_format_, std::array<byte, 3>{2, tp::hole, 0})),
          original_b_read_plan_(reader(primary_format_, std::array<byte, 3>{tp::hole, 1, 1})),
          replacement_b_read_plan_(
              reader(replacement_format_, std::array<byte, 3>{tp::hole, 0, 0})),
          ac_(required(tp::bind_reader(ac_plan_, primary_view_))),
          original_b_read_(required(tp::bind_reader(original_b_read_plan_, primary_view_))),
          replacement_b_read_(
              required(tp::bind_reader(replacement_b_read_plan_, replacement_view_))),
          state_{target.published},
          projection_{
              projection_type(ac_, target.substituted ? replacement_b_read_ : original_b_read_),
              &state_},
          law_{&state_, encoding_edition}, maintenance_(projection_, law_) {
        CHECK(residency_ && !target_->can_read());
        for (std::size_t i = 0; i < length; ++i) {
            const auto row = first + i;
            input_[i] = {std::uint64_t(new_a(row)) << 8, new_b(row)};
            if (selected(row))
                selected_words_[row / 64] |= std::uint64_t(1) << (row % 64);
        }
        // Whole retained command is admitted before its first chunk. Later
        // calls may recheck, but storage/input/selection/capacity remain stable.
        CHECK(operation_.admit(first, input_, selection(), effects_.remaining()));
        CHECK(maintenance_.covers(first, length));
    }
    retained_work(const retained_work&) = delete;
    retained_work& operator=(const retained_work&) = delete;
    retained_work(retained_work&&) = delete;
    retained_work& operator=(retained_work&&) = delete;
    ~retained_work() {
        CHECK(!residency_ && phase_ == phase::released);
    }
    phase state() const {
        return phase_;
    }
    std::size_t frontier() const {
        return next_;
    }
    const storage* address() const {
        return residency_.get();
    }
    std::size_t effect_count() const {
        return effects_.used;
    }
    const maintenance_state& staged() const {
        return state_;
    }
    std::uint64_t generation() const {
        return request_generation_;
    }
    void rotate_request() {
        CHECK(phase_ == phase::suspended);
        ++request_generation_;
        // Request rotation invalidates asynchronous replies, not the admitted
        // storage mapping. Changing the latter would require owner rebinding.
    }
    bool accept(completion reply) {
        if (phase_ != phase::suspended || reply.request_generation != request_generation_ ||
            reply.mapping != mapping_generation)
            return false;
        CHECK(reply.residency);
        prefetched_ = std::move(reply.residency);
        phase_ = phase::running;
        return true;
    }
    phase step() {
        CHECK(phase_ == phase::running && residency_ && !target_->can_read());
        const auto count = std::min(chunk, length - next_);
        CHECK(operation_.replace(first + next_, std::span(input_).subspan(next_, count), effects_,
                                 selection(), maintenance_));
        next_ += count;
        phase_ = next_ == length ? phase::complete : phase::suspended;
        return phase_;
    }
    cancellation cancel() {
        CHECK(phase_ != phase::released);
        if (next_) {
            phase_ = phase::owner_resolution;
            return cancellation::dirty_retained;
        }
        prefetched_.reset();
        target_->return_clean(std::move(residency_));
        phase_ = phase::released;
        return cancellation::clean_released;
    }
    void continue_after_cancel() {
        CHECK(phase_ == phase::owner_resolution && residency_);
        phase_ = next_ == length ? phase::complete : phase::running;
    }
    void check_frontier() const {
        CHECK(residency_ && !target_->can_read());
        check_storage(*residency_, target_->substituted, next_);
        CHECK(target_->published == initial_metadata() && target_->revision == 1 &&
              target_->used == 0);
        unsigned visited = 0;
        for (std::size_t row = 0; row < rows; ++row) {
            const bool changed = completed(row, next_);
            CHECK(state_.changed[row] == changed && state_.visits[row] == unsigned(changed));
            visited += changed;
            if (!changed)
                CHECK(state_.staged.masks[row] ==
                      signature(old_a(row), old_b(row), unchanged_c(row)));
        }
        CHECK(state_.projection_reads == (Policy == policy::bypass ? 0 : visited));
        if (next_)
            CHECK(effects_.used != 0);
    }
    bool publish() {
        if (phase_ != phase::complete || next_ != length)
            return false;
        CHECK(residency_ && !target_->can_read());
        std::array<resolved_write, journal_capacity> resolved{};
        unsigned seen_partitions = 0;
        for (std::size_t i = 0; i < effects_.used; ++i) {
            const auto& effect = effects_.storage[i];
            const bool primary = effect.source == &primary_view_;
            CHECK(primary || (target_->substituted && effect.source == &replacement_view_));
            seen_partitions |= primary ? 1 : 2;
            const auto offset = primary ? primary_offset : replacement_offset;
            const auto stride = primary ? primary_stride : replacement_stride;
            const auto bytes = primary ? 2u : 1u;
            CHECK(effect.bytes.plane == 0 && effect.bytes.size && effect.bytes.offset >= offset);
            for (std::size_t at = effect.bytes.offset; at < effect.bytes.offset + effect.bytes.size;
                 ++at) {
                const auto row = (at - offset) / stride;
                CHECK(row < rows && selected(row) && (at - offset) % stride < bytes);
            }
            resolved[i] = {primary ? primary_id : replacement_id, mapping_generation, effect.bytes};
        }
        CHECK(seen_partitions == (target_->substituted ? 3u : 1u));
        // Resolve borrowed source pointers while their named views are alive.
        target_->publish(std::move(residency_), state_.staged,
                         std::span(resolved).first(effects_.used));
        prefetched_.reset();
        phase_ = phase::released;
        return true;
    }
};

template <policy Policy> void check_published(const owner& target) {
    CHECK(target.can_read() && target.revision == 2 && target.used > 0);
    check_storage(*target.readable, target.substituted, length);
    for (std::size_t row = 0; row < rows; ++row) {
        const bool changed = selected(row);
        const byte a = changed ? new_a(row) : old_a(row), b = changed ? new_b(row) : old_b(row);
        const auto fresh = signature(a, b, unchanged_c(row));
        const auto original = signature(old_a(row), old_b(row), unchanged_c(row));
        const auto expected = Policy == policy::exact          ? fresh
                              : Policy == policy::conservative ? std::uint16_t(fresh | original)
                                                               : original;
        CHECK(target.published.masks[row] == expected);
        CHECK(target.published.row_bypass[row] == (Policy == policy::bypass && changed));
        // Check every consulted pruning level through the owner reader. New
        // high tokens were absent from the initial root and block summaries.
        for (auto value : {a, b, unchanged_c(row)})
            CHECK(target.may_contain(row, token(value)));
        const auto evidence = target.published.a_zero[row];
        CHECK(!evidence.certain || a == 0);
        CHECK(a != 0 || evidence.possible);
        if (changed && Policy != policy::exact)
            CHECK(!evidence.certain && evidence.possible);
    }
    CHECK(target.published.root_bypass == (Policy == policy::bypass));
    for (std::size_t block = 0; block < blocks; ++block) {
        bool changed = false;
        for (std::size_t row = block * block_rows; row < (block + 1) * block_rows; ++row)
            changed |= selected(row);
        CHECK(target.published.block_bypass[block] == (Policy == policy::bypass && changed));
    }
    // Durable owner identities survive destruction of the retained views.
    for (std::size_t i = 0; i < target.used; ++i) {
        CHECK(target.writes[i].generation == mapping_generation);
        CHECK(target.writes[i].partition == primary_id ||
              (target.substituted && target.writes[i].partition == replacement_id));
    }
    auto covered = [&](std::uint64_t partition, std::size_t offset) {
        for (std::size_t i = 0; i < target.used; ++i) {
            const auto& effect = target.writes[i];
            if (effect.partition == partition && offset >= effect.bytes.offset &&
                offset - effect.bytes.offset < effect.bytes.size)
                return true;
        }
        return false;
    };
    for (std::size_t row = first; row < first + length; ++row) {
        if (!selected(row))
            continue;
        CHECK(covered(primary_id, primary_offset + row * primary_stride));
        if (target.substituted)
            CHECK(covered(replacement_id, replacement_offset + row * replacement_stride));
    }
}

template <policy Policy> void retained_lifecycle(bool substituted) {
    owner target(substituted);
    const auto* address = target.readable.get();
    reply_counts replies;
    {
        retained_work<Policy> work(target);
        CHECK(work.address() == address && !target.can_read());
        CHECK(!work.publish());
        CHECK(work.step() == phase::suspended && work.frontier() == chunk);
        work.check_frontier();
        CHECK(!work.publish());
        const auto old_request = work.generation();
        completion stale{old_request, mapping_generation, std::make_unique<reply_lease>(replies)};
        work.rotate_request();
        const auto effects_before = work.effect_count();
        const auto summary_before = work.staged().staged;
        const auto reads_before = work.staged().projection_reads;
        CHECK(!work.accept(std::move(stale)));
        CHECK(!work.accept(
            {work.generation(), mapping_generation + 1, std::make_unique<reply_lease>(replies)}));
        CHECK(replies.acquired == 2 && replies.released == 2);
        CHECK(work.state() == phase::suspended && work.address() == address &&
              work.frontier() == chunk);
        CHECK(work.effect_count() == effects_before && work.staged().staged == summary_before);
        CHECK(work.staged().projection_reads == reads_before);
        CHECK(work.accept(
            {work.generation(), mapping_generation, std::make_unique<reply_lease>(replies)}));
        CHECK(replies.acquired - replies.released == 1);
        CHECK(work.step() == phase::suspended && work.frontier() == chunk * 2);
        work.check_frontier();
        const auto dirty_effects = work.effect_count();
        const auto dirty_summary = work.staged().staged;
        completion late{work.generation(), mapping_generation,
                        std::make_unique<reply_lease>(replies)};
        CHECK(work.cancel() == cancellation::dirty_retained);
        CHECK(work.address() == address && work.frontier() == chunk * 2 && !target.can_read());
        CHECK(work.effect_count() == dirty_effects && work.staged().staged == dirty_summary);
        CHECK(!work.publish());
        CHECK(replies.acquired - replies.released == 2);
        CHECK(!work.accept(std::move(late)));
        CHECK(replies.acquired - replies.released == 1);
        work.check_frontier();
        // Owner elects to finish the admitted in-place mutation. Dropping a
        // lease or a cancellation message would not undo the successful chunks.
        work.continue_after_cancel();
        CHECK(work.step() == phase::complete && work.frontier() == length);
        work.check_frontier();
        CHECK(work.publish());
        CHECK(replies.acquired == replies.released);
        CHECK(!work.publish());
    }
    CHECK(target.readable.get() == address);
    check_published<Policy>(target);
}

void clean_cancellation() {
    owner target(true);
    const auto* address = target.readable.get();
    reply_counts replies;
    {
        retained_work<policy::exact> work(target);
        completion late{work.generation(), mapping_generation,
                        std::make_unique<reply_lease>(replies)};
        CHECK(work.cancel() == cancellation::clean_released);
        CHECK(target.can_read() && target.readable.get() == address);
        CHECK(replies.acquired == 1 && replies.released == 0);
        CHECK(!work.accept(std::move(late)));
        CHECK(replies.acquired == replies.released);
        CHECK(!work.publish());
    }
    CHECK(target.revision == 1 && target.used == 0 && target.published == initial_metadata());
    check_storage(*target.readable, true, 0);
}
} // namespace

int main() {
    clean_cancellation();
    for (bool substituted : {false, true}) {
        retained_lifecycle<policy::exact>(substituted);
        retained_lifecycle<policy::conservative>(substituted);
        retained_lifecycle<policy::bypass>(substituted);
    }
    std::printf("TuplePack ownership: %zu checks passed; retained in-place chunks, stale replies, "
                "clean/dirty cancellation, exact/conservative/bypass publication, "
                "complete independent observations and substituted source identities\n",
                checks);
}
