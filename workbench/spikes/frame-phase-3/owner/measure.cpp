// B8: private preparation only. Both arms charge touched-input/output admission,
// ordered commands, exact final group statistics and one emission. Construction,
// verification, returned-image disposal, publication and durability are outside.
// The baseline attaches every group; it is not a native-container control.
#include "binding.h"
#include "../proto/change.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <source_location>
#include <span>
#include <string>
#include <vector>

namespace fo = frame3_owner;
namespace {
using frame3::u64;
using frame3::u128;
constexpr unsigned group = 7, first = group * 4096, rounds = 6, calls = 8;
constexpr fo::fitting_policy policy{0, 64, 2};
constexpr std::size_t record_capacity = 4096, output_limit = 1 << 20;
volatile u64 sink{};
void check(bool condition, const char *what,
    std::source_location where = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "%s:%u: %s\n", where.file_name(), where.line(), what);
    std::abort();
}
template <class T, class E> T take(std::expected<T, E> value,
    std::source_location where = std::source_location::current()) {
    check(bool(value), "unexpected refusal", where);
    return std::move(*value);
}
fo::description program() {
    fo::description d;
    d.nodes = {{fo::node_kind::pack, static_cast<std::uint16_t>(fo::pack_preset::compact), 0, {}},
               {fo::node_kind::bound, 0, 0, {{0, fo::coordinate::identity}}}};
    d.root = 1;
    return d;
}
u64 hash(u64 h, std::span<const std::byte> bytes) {
    for (auto x : bytes) h = (h ^ std::to_integer<unsigned>(x)) * 0x100000001b3;
    return h;
}
u64 digest(const fo::prepared_group &r) {
    u64 h = hash(0xcbf29ce484222325, r.image);
    const u128 delta = static_cast<u128>(r.contribution);
    for (u64 x : {static_cast<u64>(r.sum), static_cast<u64>(r.sum >> 64),
                  static_cast<u64>(delta), static_cast<u64>(delta >> 64), r.min, r.max,
                  u64{r.copied_bytes}, u64{r.new_bytes}, u64{r.issued_bytes}, u64{r.records}, u64{r.rebuilds}})
        h = (h ^ x) * 0x100000001b3;
    return h;
}
u64 images_digest(const std::vector<std::vector<std::byte>> &images) {
    u64 h = 0xcbf29ce484222325;
    for (const auto &image : images) h = hash(h, image);
    return h;
}
std::string quoted(const std::string &s) {
    std::string out = "\"";
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c < 32) { out += "\\u00"; out += digits[c >> 4]; out += digits[c & 15]; }
        else out += c;
    }
    return out + '"';
}

fo::prepared_group all_groups(const fo::description &d, std::size_t rows,
    const std::vector<std::vector<std::byte>> &source, std::span<const fo::command> commands) {
    check(bool(fo::validate_group(source[group], fo::image_preset::compact, true, rows, group)), "baseline input image");
    auto tree = take(fo::make_tree(d, policy));
    std::vector<std::byte> draft = source[group];
    std::vector<std::vector<std::span<const std::byte>>> images(1);
    std::vector<std::vector<std::span<std::byte>>> writable(1);
    images[0].reserve(source.size());
    writable[0].resize(source.size());
    for (unsigned g = 0; g < source.size(); ++g) {
        images[0].push_back(g == group ? std::span<const std::byte>(draft) : std::span<const std::byte>(source[g]));
        if (g == group) writable[0][g] = draft;
    }
    frame3::mutable_column m(frame3::column(std::move(tree), std::span(&rows, 1), images, &writable), false);
    fo::prepared_group out;
    out.copied_bytes = draft.size();
    for (auto c : commands) {
        out.contribution += m.set(c.row, c.value).sum;
        check(m.undeclared_bytes() == 0, "baseline effects escaped their envelope");
        out.records += m.effects.entries().size();
        for (const auto &effect : m.effects.entries()) out.issued_bytes += effect.size;
    }
    check(m.reencoded_parts == 0, "baseline unexpectedly rebuilt a whole part");
    out.rebuilds = m.reencoded_groups;
    std::vector<u64> values(std::min(std::size_t{4096}, rows - first));
    m.draft().root().read(0, first, static_cast<unsigned>(values.size()), values.data());
    out.min = ~u64{0};
    for (u64 v : values) out.sum += v, out.min = std::min(out.min, v), out.max = std::max(out.max, v);
    out.image = m.draft().image(0, group);
    check(out.image.size() <= output_limit &&
        bool(fo::validate_group(out.image, fo::image_preset::compact, true, rows, group)), "baseline output image");
    out.new_bytes = out.image.size();
    return out;
}

struct workload { std::vector<fo::command> commands; std::size_t eligible{}; };
workload commands_for(const std::vector<u64> &input, const std::vector<std::byte> &image) {
    const auto m = take(fo::validate_group(image, fo::image_preset::compact, true, input.size(), group));
    std::vector<fo::command> novel;
    for (unsigned b = 0; b < m.blocks && novel.size() < 64; ++b) {
        if (!m.lo[b] && m.width[b] == 64) continue;
        const u64 value = m.lo[b] ? m.lo[b] - 1 : u64{1} << m.width[b];
        unsigned room = policy.exception_cap > m.count[b] ? policy.exception_cap - m.count[b] : 0;
        for (unsigned r = 0; r < m.rows[b] && room && novel.size() < 64; ++r) {
            bool exceptional = false;
            for (unsigned e = m.start[b]; e < m.start[b] + m.count[b]; ++e)
                exceptional |= std::to_integer<unsigned>(image[m.positions + e]) == r;
            if (!exceptional) novel.push_back({first + b * 256 + r, value}), --room;
        }
    }
    workload out;
    out.eligible = novel.size();
    const auto n = input.size() - first;
    auto candidate = [&](unsigned i) {
        if (i < novel.size()) return novel[i];
        const unsigned r = first + static_cast<unsigned>(i % std::min(std::size_t{4096}, n));
        return fo::command{r, input[r] ^ (u64{1} << 63)};
    };
    auto restore = [&](fo::command c) { return fo::command{c.row, input[c.row]}; };
    const auto a = candidate(0), b = candidate(1), c = candidate(2), e = candidate(3);
    out.commands = {a, restore(a), a, b, c, restore(b), e, restore(a)};
    for (unsigned i = 4; out.commands.size() < 64; ++i) out.commands.push_back(candidate(i));
    return out;
}
struct expected_result {
    std::vector<u64> values;
    u128 sum{};
    __int128 contribution{};
    u64 min = ~u64{0}, max{};
};
expected_result oracle(const std::vector<u64> &input, std::span<const fo::command> commands) {
    expected_result out;
    out.values.assign(input.begin() + first, input.begin() + std::min(input.size(), std::size_t{first + 4096}));
    u128 before = 0;
    for (u64 value : out.values) before += value;
    for (auto c : commands) out.values[c.row - first] = c.value;
    for (u64 value : out.values) out.sum += value, out.min = std::min(out.min, value), out.max = std::max(out.max, value);
    out.contribution = static_cast<__int128>(out.sum) - static_cast<__int128>(before);
    return out;
}
void verify(const fo::description &d, std::size_t rows, const fo::prepared_group &result,
    const expected_result &wanted, const fo::prepared_group *reference = nullptr) {
    check(result.sum == wanted.sum && result.contribution == wanted.contribution &&
        result.min == wanted.min && result.max == wanted.max && result.new_bytes == result.image.size(),
        "wrong exact final statistics/contribution");
    const std::array borrowed{fo::borrowed_group{group, result.image}};
    const auto read = take(fo::binding::attach(d, rows, borrowed));
    std::vector<u64> values(wanted.values.size());
    check(bool(read->read(first, values)) && values == wanted.values, "wrong original-coordinate output values");
    if (reference)
        check(result.image == reference->image && result.rebuilds == reference->rebuilds &&
            result.records == reference->records && result.issued_bytes == reference->issued_bytes &&
            result.copied_bytes == reference->copied_bytes, "arms disagree on emitted bytes or core work");
}
}  // namespace

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: %s COLS_DIR COLUMN [rows=65536]\n", argv[0]);
        return 2;
    }
    std::size_t rows = 65536;
    if (argc == 4) {
        const std::string number = argv[3];
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), rows);
        check(parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size(), "invalid row count");
    }
    check(rows > first && rows <= 65536, "row count must include group7 and fit one part");
    const std::string name = argv[2];
    std::vector<u64> input(rows);
    std::ifstream file(std::string(argv[1]) + "/" + name + ".u64", std::ios::binary);
    check(bool(file) && bool(file.read(reinterpret_cast<char *>(input.data()), rows * sizeof(u64))), "short/missing input column");
    if constexpr (std::endian::native == std::endian::big)
        for (auto &v : input) v = std::byteswap(v);
    const auto d = program();
    frame3::column built(take(fo::make_tree(d, policy)), input);
    std::vector<u64> decoded(rows);
    built.read(decoded.data());
    check(decoded == input, "initial image failed direct original-row oracle");
    std::vector<std::vector<std::byte>> images;
    for (unsigned g = 0; g < (rows + 4095) / 4096; ++g) {
        images.push_back(built.image(0, g));
        check(bool(fo::validate_group(images.back(), fo::image_preset::compact, true, rows, g)), "invalid source group");
    }
    const auto source_hash = images_digest(images);
    const auto work = commands_for(input, images[group]);
    const auto column_name = quoted(name);
    for (unsigned batch : {1u, 8u, 64u}) {
        const auto commands = std::span(work.commands).first(batch);
        const auto wanted = oracle(input, commands);
        const fo::preparation_budget budget{true, images[group].size(), batch, record_capacity, output_limit, true};
        auto run = [&](unsigned arm) {
            return arm ? all_groups(d, rows, images, commands) :
                take(fo::prepare_group(d, rows, group, images[group], commands, budget, policy));
        };
        const auto reference = run(0), baseline = run(1);
        verify(d, rows, reference, wanted);
        verify(d, rows, baseline, wanted, &reference);
        if (batch >= 8 && work.eligible >= 3) check(reference.rebuilds != 0, "novelty did not exhaust slack2");
        for (unsigned round = 0; round < rounds; ++round)
            for (unsigned order = 0; order < 2; ++order) {
                const unsigned arm = (round + order) % 2;
                std::array<fo::prepared_group, calls> results;
                const auto begin = std::chrono::steady_clock::now();
                for (auto &result : results) result = run(arm);
                const auto end = std::chrono::steady_clock::now();
                const double ns = std::chrono::duration<double, std::nano>(end - begin).count() / calls;
                u64 checksum = 0xcbf29ce484222325;
                for (const auto &result : results) {
                    verify(d, rows, result, wanted, &reference);
                    checksum = (checksum ^ digest(result)) * 0x100000001b3;
                }
                check(images_digest(images) == source_hash, "private preparation changed source images");
                sink = (sink ^ checksum) * 0x100000001b3 + arm + round;
                std::printf("{\"scope\":\"private_preparation_only\",\"column\":%s,\"rows\":%zu,\"group\":7,"
                    "\"arm\":\"%s\",\"round\":%u,\"order\":%u,\"calls\":%u,\"batch\":%u,"
                    "\"ns_per_batch\":%.3f,\"ns_per_command\":%.3f,\"eligible_outlier_rows\":%zu,"
                    "\"copied_image_bytes\":%zu,\"new_image_bytes\":%zu,\"core_rebuilds\":%zu,"
                    "\"private_apply_bytes\":%zu,\"private_apply_records\":%zu,\"digest\":%llu,\"verified\":true}\n",
                    column_name.c_str(), rows, arm ? "all_groups_attach" : "group_only_prepare", round, order, calls, batch,
                    ns, ns / batch, work.eligible, reference.copied_bytes, reference.new_bytes, reference.rebuilds,
                    reference.issued_bytes, reference.records, static_cast<unsigned long long>(digest(results.back())));
            }
    }
    std::fprintf(stderr, "verified B8: touched-input/output image checks charged; construction, row-oracle verification, returned-image disposal and publication outside clock; sink=%llu\n",
        static_cast<unsigned long long>(sink));
}
