#include "embedded.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace embedded {
namespace {
constexpr std::uint32_t magic = 0x5657524e;
constexpr std::size_t header_size = 16, max_record = 64 * 1024 * 1024;
void require(bool ok) { if (!ok) throw std::system_error(errno, std::generic_category()); }
void integer(Bytes& b, std::uint64_t v, std::size_t width = 8) {
  for (std::size_t i = 0; i < width; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint64_t checksum(std::span<const std::uint8_t> bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ULL; }
  return hash;
}
struct Reader {
  std::span<const std::uint8_t> bytes;
  std::size_t offset{};
  std::uint64_t integer(std::size_t width = 8) {
    if (width > bytes.size() - offset) throw std::runtime_error("short journal body");
    std::uint64_t v{};
    for (std::size_t i = 0; i < width; ++i) v |= std::uint64_t(bytes[offset++]) << (8 * i);
    return v;
  }
  Bytes blob(std::size_t n) {
    if (n > bytes.size() - offset) throw std::runtime_error("short journal part");
    Bytes b(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + n));
    offset += n; return b;
  }
};
Bytes frame(Bytes body) {
  if (body.size() > max_record) throw std::invalid_argument("fixture record too large");
  Bytes result; integer(result, magic, 4); integer(result, body.size(), 4);
  integer(result, checksum(body)); result.insert(result.end(), body.begin(), body.end()); return result;
}
Bytes prefix(std::uint8_t tag, Id transaction, Id object, Epoch epoch, Kind kind) {
  Bytes b; integer(b, tag, 1); integer(b, transaction); integer(b, object);
  integer(b, epoch); integer(b, static_cast<std::uint8_t>(kind), 1); return b;
}
std::vector<std::size_t> pages(std::size_t bytes) {
  std::vector<std::size_t> result((bytes + views::page_size() - 1) / views::page_size());
  for (std::size_t i = 0; i < result.size(); ++i) result[i] = i;
  return result;
}
Bytes material(views::Part& part, Epoch source) {
  auto held = part.open(source, pages(part.size()));
  if (!held) throw std::runtime_error("unavailable committed fixture part");
  return Bytes(held->data(), held->data() + held->size());
}
template<class T> T view(views::Result<T> result) {
  if (!result) throw std::runtime_error("journal replay failed view invariant");
  return std::move(*result);
}
void view(views::Result<void> result) {
  if (!result) throw std::runtime_error("journal replay failed view invariant");
}
}

FilePort::FilePort(const std::string& path) : fd_(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)) {
  require(fd_ >= 0);
}
FilePort::~FilePort() { ::close(fd_); }
std::shared_ptr<Ticket> FilePort::append(Bytes bytes) {
  auto ticket = std::make_shared<Ticket>(); ticket->bytes = std::move(bytes);
  queue_.push_back(ticket); return ticket;
}
Bytes FilePort::read() {
  struct stat info{}; require(::fstat(fd_, &info) == 0);
  Bytes result(static_cast<std::size_t>(info.st_size));
  std::size_t done{};
  while (done < result.size()) {
    const auto n = ::pread(fd_, result.data() + done, result.size() - done, static_cast<off_t>(done));
    if (n < 0 && errno == EINTR) continue;
    require(n > 0); done += static_cast<std::size_t>(n);
  }
  return result;
}
void FilePort::truncate(std::size_t bytes) {
  require(::ftruncate(fd_, static_cast<off_t>(bytes)) == 0); require(::fdatasync(fd_) == 0);
}
void FilePort::write(const std::shared_ptr<Ticket>& ticket, std::size_t bytes, bool acknowledge) {
  if (ticket->status != Ticket::Status::pending ||
      std::ranges::find(queue_, ticket) == queue_.end()) throw std::invalid_argument("unknown completion");
  const auto offset = ::lseek(fd_, 0, SEEK_END); require(offset >= 0);
  std::size_t done{}; bytes = std::min(bytes, ticket->bytes.size());
  try {
    while (done < bytes) {
      const auto n = ::pwrite(fd_, ticket->bytes.data() + done, bytes - done,
          offset + static_cast<off_t>(done));
      if (n < 0 && errno == EINTR) continue;
      require(n > 0); done += static_cast<std::size_t>(n);
    }
    require(::fdatasync(fd_) == 0);
    ticket->status = acknowledge ? Ticket::Status::complete : Ticket::Status::failed;
  } catch (...) { ticket->status = Ticket::Status::failed; throw; }
}
void FilePort::complete(const std::shared_ptr<Ticket>& ticket) { write(ticket, ticket->bytes.size(), true); }
void FilePort::short_write(const std::shared_ptr<Ticket>& ticket, std::size_t bytes) { write(ticket, bytes, false); }
void FilePort::lose_ack(const std::shared_ptr<Ticket>& ticket) { write(ticket, ticket->bytes.size(), false); }
void FilePort::drain() {
  for (auto& ticket : queue_) if (ticket->status == Ticket::Status::pending) complete(ticket);
}

History::History(std::shared_ptr<Port> port, std::size_t reserve_bytes, std::shared_ptr<recycling::Pool> pool)
    : port_(std::move(port)), pool_(std::move(pool)), reserve_(reserve_bytes) { recover(); }
std::shared_ptr<Snapshot> History::at(Id object, Epoch epoch) const {
  if (epoch > frontier_) return {};
  const auto found = objects_.find(object); if (found == objects_.end()) return {};
  const auto after = found->second.upper_bound(epoch);
  return after == found->second.begin() ? nullptr : std::prev(after)->second;
}
Result<std::vector<Id>> History::layout(Id object, Epoch epoch) const {
  auto snapshot = at(object, epoch);
  if (!snapshot || !snapshot->present) return std::unexpected(Error::missing);
  std::vector<Id> result; for (auto& [id, part] : snapshot->parts) { (void)part; result.push_back(id); }
  return result;
}
Result<Epoch> History::source(Id object, Epoch epoch, Id part) const {
  auto snapshot = at(object, epoch);
  if (!snapshot || !snapshot->present || !snapshot->sources.contains(part)) return std::unexpected(Error::missing);
  return snapshot->sources.at(part);
}
Result<views::Lease> History::open(Id object, Epoch epoch, Id part,
    std::span<const std::size_t> requested, views::Method method) {
  auto snapshot = at(object, epoch);
  if (!snapshot || !snapshot->present || !snapshot->parts.contains(part)) return std::unexpected(Error::missing);
  auto held = snapshot->parts.at(part)->open(snapshot->sources.at(part), requested, method);
  if (!held) return std::unexpected(Error::view); return std::move(*held);
}
Result<std::shared_ptr<views::Fetch>> History::fetch(Id object, Epoch epoch, Id part,
    std::span<const std::size_t> requested) {
  auto snapshot = at(object, epoch);
  if (!snapshot || !snapshot->present || !snapshot->parts.contains(part)) return std::unexpected(Error::missing);
  auto fetched = snapshot->parts.at(part)->fetch(snapshot->sources.at(part), requested);
  if (!fetched) return std::unexpected(Error::view); return std::move(*fetched);
}
Result<std::unique_ptr<Mutation>> History::intend(Id object, Kind kind) {
  if (uncertain_) return std::unexpected(Error::io);
  if (busy_) return std::unexpected(Error::busy);
  auto head = at(object, frontier_);
  const bool present = head && head->present;
  if (kind == Kind::create && present) return std::unexpected(Error::exists);
  if (kind != Kind::create && !present) return std::unexpected(Error::missing);
  objects_.try_emplace(object);
  auto mutation = std::unique_ptr<Mutation>(new Mutation(*this, object, kind, next_intent_++, next_epoch_++));
  busy_ = true; return mutation;
}
void History::recover() {
  const auto log = port_->read(); std::size_t offset{};
  struct Intent { Id object; Epoch epoch; Kind kind; };
  std::map<Id, Intent> intents;
  while (log.size() - offset >= header_size) {
    Reader header{std::span(log).subspan(offset, header_size)};
    if (header.integer(4) != magic) throw std::runtime_error("bad journal frame magic");
    const auto size = static_cast<std::size_t>(header.integer(4)); const auto hash = header.integer();
    if (size > max_record) throw std::runtime_error("oversize journal frame");
    if (size > log.size() - offset - header_size) break;
    auto body = std::span(log).subspan(offset + header_size, size);
    if (checksum(body) != hash) throw std::runtime_error("corrupt complete journal frame");
    Reader r{body}; const auto tag = r.integer(1), transaction = r.integer(), object = r.integer(), epoch = r.integer();
    const auto kind_value = r.integer(1);
    if (tag < 1 || tag > 3 || kind_value > 2 || !epoch || !transaction ||
        epoch == std::numeric_limits<Epoch>::max()) throw std::runtime_error("bad journal record");
    const auto kind = static_cast<Kind>(kind_value);
    next_intent_ = std::max(next_intent_, transaction + 1); next_epoch_ = std::max(next_epoch_, epoch + 1);
    if (tag == 1) {
      if (!intents.emplace(transaction, Intent{object, epoch, kind}).second || r.offset != body.size())
        throw std::runtime_error("duplicate or malformed intent");
    } else {
      const auto intent = intents.find(transaction);
      if (intent == intents.end() || intent->second.object != object || intent->second.epoch != epoch ||
          intent->second.kind != kind) throw std::runtime_error("record without matching intent");
      if (tag == 3) {
        if (epoch <= frontier_) throw std::runtime_error("reordered committed epochs");
        auto previous = at(object, frontier_); const bool existed = previous && previous->present;
        if ((kind == Kind::create && existed) || (kind != Kind::create && !existed))
          throw std::runtime_error("invalid recovered object operation");
        auto snapshot = std::make_shared<Snapshot>(); snapshot->epoch = epoch; snapshot->present = kind != Kind::erase;
        const auto count = r.integer();
        if (count > body.size() / 24 || (kind == Kind::erase && count)) throw std::runtime_error("bad part count");
        for (std::uint64_t i = 0; i < count; ++i) {
          const auto part_id = r.integer(), source = r.integer(), bytes_size = r.integer();
          if (!source || source > epoch || bytes_size > max_record ||
              snapshot->parts.contains(part_id)) throw std::runtime_error("bad recovered part");
          auto bytes = r.blob(static_cast<std::size_t>(bytes_size));
          std::shared_ptr<views::Part> part;
          if (previous && previous->present && previous->parts.contains(part_id)) {
            part = previous->parts.at(part_id);
            if (previous->sources.at(part_id) == source) {
              if (material(*part, source) != bytes) throw std::runtime_error("unchanged revision changed bytes");
            } else {
              if (source != epoch) throw std::runtime_error("changed part without current source epoch");
              auto draft = view(part->begin(views::Method::cow, {}));
              view(draft.grow(bytes.size()));
              auto held = view(draft.access(pages(bytes.size())));
              if (!bytes.empty()) std::memcpy(held.writable(), bytes.data(), bytes.size()); held.close();
              const std::vector<views::Range> ranges{{0, bytes.size()}};
              auto delivery = view(draft.stage(ranges)); delivery->complete = true;
              auto prepared = view(draft.prepare(epoch)); view(prepared.install());
            }
          } else {
            if (source != epoch) throw std::runtime_error("new part without current source epoch");
            const auto rounded = pages(static_cast<std::size_t>(bytes_size)).size() * views::page_size();
            part = std::make_shared<views::Part>(std::move(bytes), source, false,
                std::max(reserve_, rounded), pool_);
          }
          snapshot->parts.emplace(part_id, std::move(part)); snapshot->sources.emplace(part_id, source);
        }
        if (r.offset != body.size()) throw std::runtime_error("trailing commit bytes");
        objects_[object].emplace(epoch, std::move(snapshot)); frontier_ = epoch;
      }
    }
    offset += header_size + size;
  }
  if (offset != log.size()) port_->truncate(offset);
}

Mutation::Mutation(History& owner, Id object, Kind kind, Id intent, Epoch epoch)
    : owner_(owner), object_(object), id_(intent), kind_(kind), epoch_(epoch) {
  intent_ = owner_.port_->append(frame(prefix(1, id_, object_, epoch_, kind_)));
}
Mutation::~Mutation() {
  if (!finished_) {
    // A submitted decision can outlive the caller. Reopen before taking another
    // intent, even when its completion is still unknown.
    if (decision_) owner_.uncertain_ = true;
    owner_.busy_ = false;
  }
}
Result<void> Mutation::start() {
  if (active_ || prepared_ || finished_) return std::unexpected(Error::invalid);
  if (intent_->status == Ticket::Status::pending) return std::unexpected(Error::pending);
  if (intent_->status == Ticket::Status::failed) { owner_.uncertain_ = true; return std::unexpected(Error::io); }
  candidate_ = std::make_shared<Snapshot>(); candidate_->epoch = epoch_; candidate_->present = kind_ != Kind::erase;
  if (kind_ == Kind::update) {
    auto old = owner_.at(object_, owner_.frontier_); candidate_->parts = old->parts; candidate_->sources = old->sources;
  }
  active_ = true;
  if (kind_ == Kind::create) { auto first = add(1); if (!first) return std::unexpected(first.error()); }
  return {};
}
Result<views::Draft*> Mutation::add(Id part) {
  if (!active_ || prepared_ || failed_ || kind_ == Kind::erase || candidate_->parts.contains(part)) return std::unexpected(Error::invalid);
  auto fresh = std::make_shared<views::Part>(Bytes{}, 0, false, owner_.reserve_, owner_.pool_);
  auto draft = fresh->begin(views::Method::cow, {}); if (!draft) return std::unexpected(Error::view);
  auto owned = std::make_unique<views::Draft>(std::move(*draft)); auto* result = owned.get();
  drafts_.emplace(part, std::move(owned)); candidate_->parts.emplace(part, std::move(fresh));
  candidate_->sources.emplace(part, epoch_); return result;
}
Result<views::Draft*> Mutation::edit(Id part, views::Method method, std::span<const std::size_t> requested) {
  if (!active_ || prepared_ || failed_ || kind_ == Kind::erase || !candidate_->parts.contains(part)) return std::unexpected(Error::invalid);
  if (drafts_.contains(part)) return drafts_.at(part).get();
  auto draft = candidate_->parts.at(part)->begin(method, requested); if (!draft) return std::unexpected(Error::view);
  auto owned = std::make_unique<views::Draft>(std::move(*draft)); auto* result = owned.get();
  drafts_.emplace(part, std::move(owned)); candidate_->sources.at(part) = epoch_; return result;
}
Result<void> Mutation::remove(Id part) {
  if (!active_ || prepared_ || failed_ || kind_ == Kind::erase || drafts_.contains(part) || !candidate_->parts.contains(part))
    return std::unexpected(Error::invalid);
  candidate_->parts.erase(part); candidate_->sources.erase(part); return {};
}
Result<std::shared_ptr<Ticket>> Mutation::stage(Id part, std::span<const views::Range> ranges) {
  if (!active_ || prepared_ || failed_ || !drafts_.contains(part)) return std::unexpected(Error::invalid);
  auto captured = drafts_.at(part)->stage(ranges); if (!captured) return std::unexpected(Error::view);
  auto b = prefix(2, id_, object_, epoch_, kind_); integer(b, part); integer(b, stages_.size());
  integer(b, (*captured)->patches.size());
  for (auto& [range, bytes] : (*captured)->patches) {
    integer(b, range.offset); integer(b, range.size); b.insert(b.end(), bytes.begin(), bytes.end());
  }
  auto ticket = owner_.port_->append(frame(std::move(b))); stages_.emplace_back(ticket, std::move(*captured)); return ticket;
}
Result<void> Mutation::prepare() {
  if (!active_ || prepared_ || finished_ || failed_) return std::unexpected(Error::invalid);
  for (auto& [ticket, delivery] : stages_) {
    if (ticket->status == Ticket::Status::failed) { owner_.uncertain_ = true; return std::unexpected(Error::io); }
    if (ticket->status == Ticket::Status::pending) return std::unexpected(Error::pending);
    delivery->complete = true;
  }
  // Preflight every draft before the durable decision. Failure aborts this attempt.
  for (auto& [id, draft] : drafts_) {
    auto ready = draft->prepare(epoch_);
    if (!ready) { failed_ = true; prepared_parts_.clear(); return std::unexpected(Error::view); }
    prepared_parts_.emplace(id, std::move(*ready));
  }
  auto b = prefix(3, id_, object_, epoch_, kind_); integer(b, candidate_->parts.size());
  for (auto& [id, part] : candidate_->parts) {
    Bytes unchanged;
    const Bytes* bytes;
    if (prepared_parts_.contains(id)) bytes = &prepared_parts_.at(id).bytes();
    else { unchanged = material(*part, candidate_->sources.at(id)); bytes = &unchanged; }
    integer(b, id); integer(b, candidate_->sources.at(id)); integer(b, bytes->size());
    b.insert(b.end(), bytes->begin(), bytes->end());
  }
  decision_bytes_ = frame(std::move(b));
  History::Revisions storage; storage.emplace(epoch_, candidate_); head_ = storage.extract(epoch_);
  prepared_ = true; return {};
}
Result<void> Mutation::commit() {
  if (!prepared_ || finished_) return std::unexpected(Error::invalid);
  if (!decision_) { decision_ = owner_.port_->append(std::move(decision_bytes_)); return std::unexpected(Error::pending); }
  if (decision_->status == Ticket::Status::pending) return std::unexpected(Error::pending);
  if (decision_->status == Ticket::Status::failed) { owner_.uncertain_ = true; return std::unexpected(Error::io); }
  for (auto& [id, ready] : prepared_parts_) {
    (void)id; if (!ready.install()) { owner_.uncertain_ = true; return std::unexpected(Error::view); }
  }
  owner_.objects_.at(object_).insert(std::move(head_)); owner_.frontier_ = epoch_;
  owner_.busy_ = false; finished_ = true; return {};
}
}
