#pragma once
#include "prototype.h"
#include <functional>

namespace acquisition {
using Completion = std::function<void(bool)>;
class Port {
 public:
  virtual ~Port() = default;
  // Retain Fetch through actual completion and call complete once.
  // True means every requested destination was filled; false means failure.
  virtual void read(std::shared_ptr<views::Fetch>, Completion) = 0;
};
bool read_file(views::Fetch&);
bool read_mapping(views::Fetch&);
class FilePort final : public Port {
 public:
  void read(std::shared_ptr<views::Fetch>, Completion) override;
};
class MappingPort final : public Port {
 public:
  void read(std::shared_ptr<views::Fetch>, Completion) override;
};
enum class Status { idle, pending, ready, failed, cancelled };
struct Slot;
class Acquisition {
 public:
  Acquisition();
  views::Result<void> start(views::Part&, views::Epoch,
                            std::span<const std::size_t> pages, Port&);
  void cancel();
  Status status() const;
  views::Result<views::Lease> lease() const;
 private:
  std::shared_ptr<Slot> slot_;
};
}
