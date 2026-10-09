// Experiments in return shape, not a proposed public API or lifetime shortcut.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

struct Owner { unsigned references{}, retains{}, releases{}; };
void retain(Owner* p) { if (p) { ++p->references; ++p->retains; } }
void release(Owner* p) { if (p) { if (!p->references) std::abort(); --p->references; ++p->releases; } }

struct Descriptor; // A real descriptor would own the immutable logical binding.
struct Selected8 { const Descriptor* descriptor{}; };
struct Ready16 { Selected8 selected; std::uint32_t error{}; };

struct OrdinaryOwner8 {
  Owner* owner{};
  explicit OrdinaryOwner8(Owner* p) : owner(p) { retain(p); }
  OrdinaryOwner8(OrdinaryOwner8&& p) noexcept : owner(std::exchange(p.owner, nullptr)) {}
  ~OrdinaryOwner8() { release(owner); }
};

struct [[clang::trivial_abi]] RegisterOwner8 {
  Owner* owner{};
  explicit RegisterOwner8(Owner* p) : owner(p) { retain(p); }
  RegisterOwner8(RegisterOwner8&& p) noexcept : owner(std::exchange(p.owner, nullptr)) {}
  ~RegisterOwner8() { release(owner); }
};

struct [[clang::trivial_abi]] RegisterResult16 {
  Owner* owner{};
  std::uint32_t error{};
  RegisterResult16(Owner* p, std::uint32_t e) : owner(e ? nullptr : p), error(e) { retain(owner); }
  RegisterResult16(RegisterResult16&& p) noexcept
      : owner(std::exchange(p.owner, nullptr)), error(p.error) {}
  ~RegisterResult16() { release(owner); }
};

extern "C" {
[[gnu::noinline]] Ready16 candidate_select(const Descriptor* d, std::uint32_t error) {
  return {{error ? nullptr : d}, error};
}
[[gnu::noinline]] OrdinaryOwner8 ordinary_owner(Owner* p) { return OrdinaryOwner8(p); }
[[gnu::noinline]] RegisterOwner8 register_owner(Owner* p) { return RegisterOwner8(p); }
[[gnu::noinline]] RegisterResult16 register_result(Owner* p, std::uint32_t error) {
  return RegisterResult16(p, error);
}
[[gnu::noinline]] bool consume_owner(RegisterOwner8 p) { return p.owner->references != 0; }
}

int main() {
  Owner owner;
  {
    auto a = ordinary_owner(&owner);
    auto b = register_owner(&owner);
    auto c = register_result(&owner, 0);
    auto failed = register_result(&owner, 3);
    if (owner.references != 3 || failed.owner || failed.error != 3) std::abort();
    if (!consume_owner(std::move(b)) || b.owner || owner.references != 2) std::abort();
  }
  if (owner.references || owner.retains != 3 || owner.releases != 3) std::abort();
  std::printf("selected=%zu ready=%zu ordinary_owner=%zu register_owner=%zu register_result=%zu; releases=%u\n",
      sizeof(Selected8), sizeof(Ready16), sizeof(OrdinaryOwner8), sizeof(RegisterOwner8),
      sizeof(RegisterResult16), owner.releases);
}
