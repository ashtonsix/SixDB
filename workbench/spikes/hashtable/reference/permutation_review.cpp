// Standalone width-preserving permutation diagnostics, not a table benchmark.
// Build with the pinned compiler; both compiler vectorizers must be disabled.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <string_view>
#include <unordered_set>
#include <vector>

using word = std::uint64_t;
constexpr word low(unsigned n) { return n == 64 ? ~word{} : (word{1} << n) - 1; }
void check(bool good) { if (!good) std::abort(); }
word seed_mix(word x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
word odd_inverse(word x) {
    word out = x;
    for (unsigned i = 0; i < 6; ++i) out *= 2 - x * out;
    return out;
}
word modulus_inverse(word value, word modulus) {
    // Cold descriptor preparation; coefficients fit in signed 128 bits.
    __int128 old_t = 0, t = 1;
    word old_r = modulus, r = value;
    while (r) {
        word q = old_r / r;
        word next_r = old_r % r; old_r = r; r = next_r;
        __int128 next_t = old_t - __int128(q) * t; old_t = t; t = next_t;
    }
    check(old_r == 1);
    if (old_t < 0) old_t += modulus;
    return word(old_t);
}
template<unsigned W> [[gnu::always_inline]] word ones_multiply(word x, word m) {
    // End-around carry implements modulo(2^W-1), preserving BOTH encodings
    // of zero: 0 stays 0, all-ones stays all-ones. With a coprime multiplier no
    // interior input maps to either, giving a permutation of all 2^W values.
    const __uint128_t product = __uint128_t(x) * m;
    const word a = word(product) & low(W), b = word(product >> W);
    const word sum = a + b;
    if constexpr (W == 64) return sum + (sum < a);
    else return (sum & low(W)) + (sum >> W);
}
template<unsigned W, unsigned Shift> word undo_right(word x) {
    word out = x;
    for (unsigned s = Shift; s < W; s += Shift) out ^= x >> s;
    return out & low(W);
}
enum family { multiply, premix_multiply, splitmix, feistel3, feistel4, quotient_fold, quotient_fold_mix,
              quotient_strong, quotient_strong_mix, feistel3_strong, feistel4_strong, ones };
constexpr const char* names[]{"xor_random_odd", "premix_random_odd", "width_splitmix", "feistel3_light", "feistel4_light", "quotient_fold", "quotient_fold_mixed_remainder",
    "quotient_mixed_hash", "quotient_mixed_hash_and_remainder", "feistel3_mix32", "feistel4_mix32", "ones_complement_multiply"};

template<unsigned W, family F, unsigned Address = std::min(10u, W / 2 - 2)> struct permutation {
    static constexpr unsigned H = W / 2;
    static constexpr unsigned A = Address;
    static constexpr unsigned R = W - A;
    static constexpr unsigned S0 = std::max(1u, W * 30 / 64);
    static constexpr unsigned S1 = std::max(1u, W * 27 / 64);
    static constexpr unsigned S2 = std::max(1u, W * 31 / 64);
    static constexpr word M0 = 0xbf58476d1ce4e5b9ULL & low(W);
    static constexpr word M1 = 0x94d049bb133111ebULL & low(W);
    static constexpr bool quotient = F == quotient_fold || F == quotient_fold_mix || F == quotient_strong || F == quotient_strong_mix;
    static constexpr bool mixed_remainder = F == quotient_fold_mix || F == quotient_strong_mix;
    word input_seed, multiplier, inverse_multiplier, hash_seed, hash_multiplier;
    std::array<word, 4> round_key, round_multiplier, round_add;
    explicit permutation(word seed) {
        std::mt19937_64 rng(seed);
        input_seed = rng() & low(W); multiplier = (rng() & low(W)) | 1;
        inverse_multiplier = odd_inverse(multiplier) & low(W);
        if constexpr (F == ones) {
            do { multiplier = rng() & low(W); }
            while (multiplier <= 1 || multiplier == low(W) || std::gcd(multiplier,low(W)) != 1);
            inverse_multiplier = modulus_inverse(multiplier,low(W));
        }
        hash_seed = rng(); hash_multiplier = rng() | 1;
        for (unsigned r = 0; r < 4; ++r) {
            round_key[r] = rng() & (F == feistel3_strong || F == feistel4_strong ? low(32) : low(H));
            round_multiplier[r] = (rng() & low(H)) | 1;
            round_add[r] = rng() & low(W);
        }
    }
    [[gnu::always_inline]] word round(word right, unsigned r) const {
        if constexpr (F == feistel3_strong || F == feistel4_strong)
            return mix32(std::uint32_t(right ^ round_key[r])) & low(H);
        word z = ((right ^ round_key[r]) * round_multiplier[r] + round_add[r]) & low(H);
        return z ^ (z >> (H/2));
    }
    [[gnu::always_inline]] word quotient_hash(word r) const {
        if constexpr (F == quotient_strong || F == quotient_strong_mix) {
            if constexpr (R <= 32) return mix32(std::uint32_t(r ^ hash_seed)) >> (32-A);
            else return seed_mix(r ^ hash_seed) >> (64-A);
        }
        const __uint128_t product = __uint128_t(r ^ hash_seed) * hash_multiplier;
        return (word(product) ^ word(product >> 64)) >> (64-A);
    }
    [[gnu::always_inline]] word operator()(word x) const {
        x ^= input_seed;
        if constexpr (F == ones) return ones_multiply<W>(x,multiplier);
        else if constexpr (F == multiply) return (x * multiplier) & low(W);
        else if constexpr (F == premix_multiply) {
            // Mixing BEFORE multiplication affects bucket bits. A final right
            // xor alone leaves the top half unchanged and cannot repair them.
            x ^= x >> H;
            return (x * multiplier) & low(W);
        } else if constexpr (F == splitmix) {
            x ^= x >> S0; x = (x * M0) & low(W);
            x ^= x >> S1; x = (x * M1) & low(W);
            return x ^ (x >> S2);
        } else if constexpr (quotient) {
            word r = x >> A;
            word b = (x & low(A)) ^ quotient_hash(r);
            if constexpr (mixed_remainder) r = (r * multiplier) & low(R);
            return (b << R) | r;
        } else {
            word left = x >> H, right = x & low(H);
            constexpr unsigned rounds = F == feistel3 || F == feistel3_strong ? 3 : 4;
            for (unsigned r = 0; r < rounds; ++r) {
                word next = left ^ round(right, r);
                left = right; right = next;
            }
            return (left << H) | right;
        }
    }
    [[gnu::always_inline]] word inverse(word x) const {
        if constexpr (F == ones) return ones_multiply<W>(x,inverse_multiplier) ^ input_seed;
        else if constexpr (F == multiply) return ((x * inverse_multiplier) & low(W)) ^ input_seed;
        else if constexpr (F == premix_multiply)
            return undo_right<W,H>((x * inverse_multiplier) & low(W)) ^ input_seed;
        else if constexpr (F == splitmix) {
            x = undo_right<W,S2>(x); x = (x * odd_inverse(M1)) & low(W);
            x = undo_right<W,S1>(x); x = (x * odd_inverse(M0)) & low(W);
            return undo_right<W,S0>(x) ^ input_seed;
        } else if constexpr (quotient) {
            word r = x & low(R);
            if constexpr (mixed_remainder) r = (r * inverse_multiplier) & low(R);
            word b = (x >> R) ^ quotient_hash(r);
            return ((r << A) | b) ^ input_seed;
        } else {
            word left = x >> H, right = x & low(H);
            constexpr unsigned rounds = F == feistel3 || F == feistel3_strong ? 3 : 4;
            for (unsigned r = rounds; r-- > 0;) {
                word old_left = right ^ round(left, r);
                right = left; left = old_left;
            }
            return ((left << H) | right) ^ input_seed;
        }
    }
};

word reverse(word x, unsigned bits) {
    word out = 0;
    for (unsigned i = 0; i < bits; ++i) { out = (out << 1) | (x & 1); x >>= 1; }
    return out;
}
template<unsigned W> std::vector<word> keys_for(std::size_t n, unsigned shape) {
    std::vector<word> keys; keys.reserve(n);
    const unsigned used = std::bit_width(n - 1);
    if (shape == 0) {
        std::mt19937_64 rng(0x6b657973 + W);
        std::unordered_set<word> seen;
        while (keys.size() < n) { word x = rng() & low(W); if (seen.insert(x).second) keys.push_back(x); }
    } else for (word i = 0; i < n; ++i) {
        if (shape == 1) keys.push_back(i);
        if (shape == 2) keys.push_back(i << (W - used));
        if (shape == 3) keys.push_back(i | ((seed_mix(W) & low(W-used)) << used));
        if (shape == 4) keys.push_back(reverse(i, W));
        if (shape == 5) keys.push_back(i ^ (i >> 1));
    }
    return keys;
}
constexpr const char* shapes[]{"uniform", "sequential", "stride", "fixed_high_prefix", "bit_reverse", "gray", "known_primary_collision"};

template<unsigned W, family F> void verify() {
    const unsigned total = W <= 16 ? (1u << W) : 65536;
    for (unsigned s = 0; s < 4; ++s) {
        permutation<W,F> p(seed_mix(s + W));
        std::mt19937_64 rng(371 + s);
        for (unsigned i = 0; i < total; ++i) {
            word x = W <= 16 ? i : rng() & low(W);
            word y = p(x);
            check(!(y & ~low(W)) && p.inverse(y) == x && p(p.inverse(x)) == x);
        }
        for (word x : {word{0}, low(W), word{1} << (W-1)}) check(p.inverse(p(x)) == x);
    }
}

template<unsigned W, family F> void quality(unsigned seeds) {
    constexpr unsigned A = std::min(10u, W / 2 - 2);
    constexpr unsigned Q = A > 3 ? A - 3 : 1;
    constexpr unsigned FP = std::min(8u, W-A);
    constexpr unsigned SUFFIX = W-A-FP;
    for (unsigned slots : {8u, 16u}) {
        const std::size_t n = ((std::size_t{1} << A) * slots * 19) / 20;
        for (unsigned shape = 0; shape < 7; ++shape) {
            auto input = shape == 6 ? std::vector<word>{} : keys_for<W>(n, shape);
            for (unsigned s = 0; s < seeds; ++s) {
                // Separate deterministic draws; not production entropy machinery.
                std::mt19937_64 rng(0x73656564 + s);
                permutation<W,F> p(rng()); permutation<W,F,Q> q(rng());
                if (shape == 6) {
                    input.clear();
                    const std::size_t limit = std::min<std::size_t>(n, low(W-A));
                    for (std::size_t i = 0; i < limit; ++i) input.push_back(p.inverse(i));
                }
                std::vector<unsigned> primary(1u << A), secondary(1u << Q);
                std::vector<unsigned> fingerprints((1u << A) * (1u << FP));
                std::size_t spilled = 0, chained = 0, bucket_pairs = 0, fp_pairs = 0;
                for (word key : input) {
                    check(q.inverse(q(key)) == key);
                    word h = p(key); unsigned b = h >> (W-A);
                    unsigned fp = (h >> SUFFIX) & low(FP);
                    bucket_pairs += primary[b]; fp_pairs += fingerprints[(b << FP) + fp]++;
                    if (primary[b]++ < slots) continue;
                    ++spilled;
                    unsigned target = q(key) >> (W-Q);
                    if (secondary[target]++ >= slots) ++chained;
                }
                unsigned pmax = *std::max_element(primary.begin(), primary.end());
                unsigned qmax = *std::max_element(secondary.begin(), secondary.end());
                unsigned used = std::count_if(secondary.begin(), secondary.end(), [](unsigned n){ return n != 0; });
                double chi = 0, expected = double(spilled) / secondary.size();
                if (spilled) for (unsigned x : secondary) chi += (x-expected)*(x-expected)/expected;
                std::printf("quality,%u,%s,%u,%u,%u,%s,%u,%zu,%zu,%zu,%u,%u,%u,%zu,%zu,%.6f\n",
                    W,names[F],slots,A,Q,shapes[shape],s,input.size(),spilled,chained,pmax,qmax,used,bucket_pairs,fp_pairs,chi);
            }
        }
    }
}

template<unsigned W, family F, bool Inverse, bool Serial> [[gnu::noinline]] word kernel(
        const permutation<W,F>& p, std::size_t n, word seed) {
    // Keep seed/control preparation outside the timed recurrence. This private
    // copy also prevents the compiler's inline-asm alias conservatism from
    // repeatedly loading the caller's descriptor inside the serial loop.
    const permutation<W,F> fixed = p;
    auto step = [&](word x){ if constexpr (Inverse) return fixed.inverse(x); else return fixed(x); };
    if constexpr (Serial) {
        word x = seed & low(W);
        for (std::size_t i = 0; i < n; ++i) { x = step(x); asm volatile("" : "+r"(x)); }
        return x;
    } else {
        word a=seed&low(W),b=(seed+1)&low(W),c=(seed+2)&low(W),d=(seed+3)&low(W);
        word e=(seed+4)&low(W),f=(seed+5)&low(W),g=(seed+6)&low(W),h=(seed+7)&low(W);
        for (std::size_t i=0; i<n; i+=8) {
            a=step(a);b=step(b);c=step(c);d=step(d);e=step(e);f=step(f);g=step(g);h=step(h);
            asm volatile("" : "+r"(a),"+r"(b),"+r"(c),"+r"(d),"+r"(e),"+r"(f),"+r"(g),"+r"(h));
        }
        return a^b^c^d^e^f^g^h;
    }
}
template<unsigned W, family F, bool Inverse, bool Serial> void timing(std::size_t n, unsigned repeat) {
    permutation<W,F> p(seed_mix(371 + repeat));
    auto warm = kernel<W,F,Inverse,Serial>(p, 16384, 827);
    asm volatile("" :: "r"(warm));
    auto begin = std::chrono::steady_clock::now();
    word result = kernel<W,F,Inverse,Serial>(p, n, 827);
    asm volatile("" : "+r"(result) :: "memory");
    auto end = std::chrono::steady_clock::now();
    double ns = std::chrono::duration<double,std::nano>(end-begin).count() / n;
    std::printf("timing,%u,%s,%s,%s,%u,%zu,%.6f,%llu\n",W,names[F],Inverse?"inverse":"forward",
        Serial?"serial":"independent8",repeat,n,ns,static_cast<unsigned long long>(result));
}
template<unsigned W, family F> void run(std::string_view mode, unsigned amount) {
    verify<W,F>();
    if (mode == "quality") quality<W,F>(amount);
    if (mode == "timing") for (unsigned r=0; r<amount; ++r) {
        timing<W,F,false,true>(1u<<21,r); timing<W,F,false,false>(1u<<21,r);
        timing<W,F,true,true>(1u<<21,r); timing<W,F,true,false>(1u<<21,r);
    }
}
template<unsigned W> void widths(std::string_view mode, unsigned amount) {
    run<W,multiply>(mode,amount); run<W,premix_multiply>(mode,amount);
    run<W,splitmix>(mode,amount); run<W,feistel3>(mode,amount); run<W,feistel4>(mode,amount);
    run<W,quotient_fold>(mode,amount); run<W,quotient_fold_mix>(mode,amount);
    run<W,quotient_strong>(mode,amount); run<W,quotient_strong_mix>(mode,amount);
    run<W,feistel3_strong>(mode,amount); run<W,feistel4_strong>(mode,amount); run<W,ones>(mode,amount);
}
int main(int argc, char** argv) {
    if (argc != 3) { std::fputs("usage: permutation_review {check|quality|timing} REPETITIONS\n",stderr); return 2; }
    std::string_view mode(argv[1]); unsigned amount=std::strtoul(argv[2],nullptr,10);
    check(amount && (mode=="check" || mode=="quality" || mode=="timing"));
    if(mode=="quality") std::puts("record,width,family,slots,primary_bits,secondary_bits,shape,seed,n,spilled,chained,primary_max,secondary_max,secondary_nonempty,bucket_pairs,fp_pairs,conditional_chi_square");
    if(mode=="timing") std::puts("record,width,family,direction,execution,rep,operations,ns_per_key,checksum");
    widths<8>(mode,amount); widths<16>(mode,amount); widths<24>(mode,amount); widths<32>(mode,amount);
    widths<40>(mode,amount); widths<48>(mode,amount); widths<56>(mode,amount); widths<64>(mode,amount);
    std::fprintf(stderr,"verified: all twelve families, all byte widths, four seeds; exhaustive8/16 and sampled wider bidirectional inverses\n");
}
