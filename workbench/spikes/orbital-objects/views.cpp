// Actual local UFFD mapping paths; no remote protocol, journal, or timings.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/memfd.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
std::size_t page_bytes = 0;

void require(bool ok) {
  if (!ok) ::_exit(1);
}

unsigned char* map(int fd, off_t offset, int prot = PROT_READ | PROT_WRITE, void* at = nullptr) {
  void* result = ::mmap(at, page_bytes, prot, MAP_SHARED | (at ? MAP_FIXED : 0), fd, offset);
  require(result != MAP_FAILED);
  return static_cast<unsigned char*>(result);
}

int backing() {
  const int fd = static_cast<int>(::syscall(SYS_memfd_create, "orbital-object-probe", MFD_CLOEXEC));
  require(fd >= 0);
  require(::ftruncate(fd, static_cast<off_t>(page_bytes * 2)) == 0);
  return fd;
}

int userfault(std::uint64_t features) {
  const int fd = static_cast<int>(::syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY));
  if (fd < 0) ::_exit(77);
  uffdio_api api{};
  api.api = UFFD_API;
  api.features = features;
  if (::ioctl(fd, UFFDIO_API, &api) != 0 || (api.features & features) != features) ::_exit(77);
  return fd;
}

void register_range(int fd, void* address, std::uint64_t mode) {
  uffdio_register request{};
  request.range.start = reinterpret_cast<std::uintptr_t>(address);
  request.range.len = page_bytes;
  request.mode = mode;
  if (::ioctl(fd, UFFDIO_REGISTER, &request) != 0) ::_exit(77);
}

uffd_msg event(int fd) {
  pollfd ready{fd, POLLIN, 0};
  require(::poll(&ready, 1, 2000) == 1 && (ready.revents & POLLIN));
  uffd_msg message{};
  require(::read(fd, &message, sizeof(message)) == static_cast<ssize_t>(sizeof(message)));
  require(message.event == UFFD_EVENT_PAGEFAULT);
  return message;
}

void missing() {
  const int fd = backing();
  auto* view = map(fd, 0, PROT_READ);
  const int uffd = userfault(UFFD_FEATURE_MISSING_SHMEM);
  register_range(uffd, view, UFFDIO_REGISTER_MODE_MISSING);
  std::thread handler([&] {
    const auto message = event(uffd);
    require(!(message.arg.pagefault.flags & (UFFD_PAGEFAULT_FLAG_MINOR | UFFD_PAGEFAULT_FLAG_WP)));
    void* source = ::mmap(nullptr, page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    require(source != MAP_FAILED);
    std::memset(source, 17, page_bytes);
    uffdio_copy copy{};
    copy.dst = reinterpret_cast<std::uintptr_t>(view);
    copy.src = reinterpret_cast<std::uintptr_t>(source);
    copy.len = page_bytes;
    require(::ioctl(uffd, UFFDIO_COPY, &copy) == 0);
    ::munmap(source, page_bytes);
  });
  require(*static_cast<volatile unsigned char*>(view) == 17);
  handler.join();
  require(view[page_bytes - 1] == 17);
}

void minor() {
  const int fd = backing();
  auto* source = map(fd, 0);
  std::memset(source, 23, page_bytes);
  auto* view = map(fd, 0, PROT_READ);
  const int uffd = userfault(UFFD_FEATURE_MINOR_SHMEM);
  register_range(uffd, view, UFFDIO_REGISTER_MODE_MINOR);
  std::thread handler([&] {
    const auto message = event(uffd);
    require(message.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_MINOR);
    uffdio_continue supply{};
    supply.range.start = reinterpret_cast<std::uintptr_t>(view);
    supply.range.len = page_bytes;
    require(::ioctl(uffd, UFFDIO_CONTINUE, &supply) == 0);
  });
  require(*static_cast<volatile unsigned char*>(view) == 23);
  handler.join();
  require(view[page_bytes - 1] == 23);
}

void cow() {
  const int fd = backing();
  auto* base = map(fd, 0);
  std::memset(base, 31, page_bytes);
  auto* reader = map(fd, 0, PROT_READ);
  auto* writer = map(fd, 0);
  require(*static_cast<volatile unsigned char*>(writer) == 31);
  const int uffd = userfault(UFFD_FEATURE_WP_HUGETLBFS_SHMEM | UFFD_FEATURE_PAGEFAULT_FLAG_WP);
  register_range(uffd, writer, UFFDIO_REGISTER_MODE_WP);
  uffdio_writeprotect protect{};
  protect.range.start = reinterpret_cast<std::uintptr_t>(writer);
  protect.range.len = page_bytes;
  protect.mode = UFFDIO_WRITEPROTECT_MODE_WP;
  require(::ioctl(uffd, UFFDIO_WRITEPROTECT, &protect) == 0);
  std::thread handler([&] {
    const auto message = event(uffd);
    require(message.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP);
    auto* fresh = map(fd, static_cast<off_t>(page_bytes));
    std::memcpy(fresh, base, page_bytes);
    require(map(fd, static_cast<off_t>(page_bytes), PROT_READ | PROT_WRITE, writer) == writer);
    uffdio_range wake{reinterpret_cast<std::uintptr_t>(writer), page_bytes};
    // Replacing the mapping removes its old registration; wake any remaining waiter.
    const int result = ::ioctl(uffd, UFFDIO_WAKE, &wake);
    require(result == 0 || errno == EINVAL);
  });
  *static_cast<volatile unsigned char*>(writer) = 47;
  handler.join();
  require(reader[0] == 31 && reader[page_bytes - 1] == 31);
  require(writer[0] == 47 && writer[page_bytes - 1] == 31);
  // An old reader that first maps AFTER the write still gets the old backing.
  auto* late = map(fd, 0, PROT_READ);
  require(late[0] == 31 && late[page_bytes - 1] == 31);
}

void reuse_and_reconstruct() {
  const int fd = backing();
  auto* writer = map(fd, 0);
  // Toy retained base plus accepted operation: 17 + 5 at logical position 10.
  std::memset(writer, 22, page_bytes);
  *static_cast<volatile unsigned char*>(writer) = 47;  // Tentative position 20.
  auto* old = map(fd, static_cast<off_t>(page_bytes));
  std::memset(old, 17, page_bytes);
  for (std::size_t i = 0; i < page_bytes; ++i) old[i] += 5;
  require(::mprotect(old, page_bytes, PROT_READ) == 0);
  require(old[0] == 22 && old[page_bytes - 1] == 22 && writer[0] == 47);
  // Abort discards the tentative projection, then reacquires reconstructed state.
  require(::munmap(writer, page_bytes) == 0);
  auto* reopened = map(fd, static_cast<off_t>(page_bytes), PROT_READ);
  require(reopened[0] == 22);
}

int run(const char* name, void (*test)()) {
  const pid_t child = ::fork();
  require(child >= 0);
  if (child == 0) {
    const rlimit no_core{0, 0};
    ::setrlimit(RLIMIT_CORE, &no_core);
    ::alarm(5);  // A broken fault handler is a bounded failed case, not a hung run.
    test();
    ::_exit(0);
  }
  int status = 0;
  require(::waitpid(child, &status, 0) == child);
  const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  std::printf("{\"case\":\"%s\",\"result\":\"%s\",\"exit_code\":%d,\"page_bytes\":%zu}\n",
              name, code == 0 ? "pass" : code == 77 ? "unsupported" : "fail", code, page_bytes);
  return code == 0 || code == 77 ? 0 : 1;
}
}

int main() {
  const long discovered = ::sysconf(_SC_PAGESIZE);
  require(discovered > 0);
  page_bytes = static_cast<std::size_t>(discovered);
  int failures = 0;
  failures += run("missing_page", missing);
  failures += run("minor_shared_page", minor);
  failures += run("wp_cow_old_and_late_readers", cow);
  failures += run("exclusive_reuse_reconstructed_old_view_abort", reuse_and_reconstruct);
  return failures ? 1 : 0;
}
