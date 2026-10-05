#include "acquisition.h"
#include <cerrno>
#include <cstring>
#include <mutex>
#include <optional>
#include <sys/mman.h>
#include <unistd.h>

namespace acquisition {
bool read_file(views::Fetch& fetch) {
  for (auto page : fetch.pages()) {
    auto destination = fetch.destination(page);
    std::size_t done{};
    while (done < destination.size()) {
      const auto n = ::pread(fetch.source_fd(), destination.data() + done,
          destination.size() - done,
          static_cast<off_t>(page * views::page_size() + done));
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return false;
      done += static_cast<std::size_t>(n);
    }
  }
  return true;
}
bool read_mapping(views::Fetch& fetch) {
  for (auto page : fetch.pages()) {
    auto destination = fetch.destination(page);
    void* source = ::mmap(nullptr, destination.size(), PROT_READ, MAP_PRIVATE,
        fetch.source_fd(), static_cast<off_t>(page * views::page_size()));
    if (source == MAP_FAILED) return false;
    std::memcpy(destination.data(), source, destination.size());
    if (::munmap(source, destination.size()) != 0) return false;
  }
  return true;
}
void FilePort::read(std::shared_ptr<views::Fetch> fetch, Completion complete) {
  const bool success = read_file(*fetch);
  complete(success);
}
void MappingPort::read(std::shared_ptr<views::Fetch> fetch, Completion complete) {
  const bool success = read_mapping(*fetch);
  complete(success);
}
struct Slot {
  std::mutex mutex;
  std::uint64_t generation{};
  Status status{Status::idle};
  std::optional<views::Lease> ready;
};
Acquisition::Acquisition() : slot_(std::make_shared<Slot>()) {}
views::Result<void> Acquisition::start(views::Part& part, views::Epoch epoch,
                                     std::span<const std::size_t> pages, Port& port) {
  auto fetched = part.fetch(epoch, pages);
  std::uint64_t generation;
  {
    std::lock_guard lock(slot_->mutex);
    generation = ++slot_->generation;
    slot_->ready.reset();
    slot_->status = fetched ? Status::pending : Status::failed;
  }
  if (!fetched) return std::unexpected(fetched.error());
  auto fetch = std::move(*fetched);
  port.read(fetch, [slot = std::weak_ptr<Slot>(slot_), generation, fetch](bool complete) {
    auto owner = slot.lock();
    bool accepted{};
    if (owner) {
      std::lock_guard lock(owner->mutex);
      accepted = owner->generation == generation && owner->status == Status::pending;
    }
    // Cancellation revokes installation, not the backend's outstanding use.
    auto result = fetch->finish(complete && accepted);
    if (!owner || !accepted) return;
    std::lock_guard lock(owner->mutex);
    if (owner->generation != generation || owner->status != Status::pending) return;
    if (result) { owner->ready.emplace(std::move(*result)); owner->status = Status::ready; }
    else owner->status = Status::failed;
  });
  return {};
}
void Acquisition::cancel() {
  std::lock_guard lock(slot_->mutex);
  ++slot_->generation;
  slot_->ready.reset();
  slot_->status = Status::cancelled;
}
Status Acquisition::status() const {
  std::lock_guard lock(slot_->mutex);
  return slot_->status;
}
views::Result<views::Lease> Acquisition::lease() const {
  std::lock_guard lock(slot_->mutex);
  if (slot_->ready) return *slot_->ready;
  return std::unexpected(slot_->status == Status::pending ? views::Error::pending : views::Error::unavailable);
}
}
