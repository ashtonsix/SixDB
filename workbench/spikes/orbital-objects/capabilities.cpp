// Availability and page-protection probe, not a sandbox or performance test.
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

int main() {
  utsname machine{};
  if (::uname(&machine) != 0) return 1;
  const long page_size = ::sysconf(_SC_PAGESIZE);
  if (page_size <= 0) return 1;
  auto* page = static_cast<volatile unsigned char*>(::mmap(
      nullptr, static_cast<std::size_t>(page_size), PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
  if (page == MAP_FAILED) return 1;
  page[0] = 17;
  if (::mprotect(const_cast<unsigned char*>(page), static_cast<std::size_t>(page_size), PROT_READ) != 0) return 1;
  const pid_t child = ::fork();
  if (child == -1) return 1;
  if (child == 0) {
    const rlimit no_core{0, 0};
    ::setrlimit(RLIMIT_CORE, &no_core);
    ::alarm(2);
    page[0] = 18;
    ::_exit(2);
  }
  int status = 0;
  if (::waitpid(child, &status, 0) != child) return 1;
  const bool read_only_enforced = WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
  const bool parent_unchanged = page[0] == 17;
  ::munmap(const_cast<unsigned char*>(page), static_cast<std::size_t>(page_size));

  errno = 0;
  const int fault_fd = static_cast<int>(::syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY));
  int fault_error = fault_fd < 0 ? errno : 0;
  uffdio_api api{};
  api.api = UFFD_API;
  // Probe without requiring optional features; registration still needs testing.
  if (fault_fd >= 0) {
    if (::ioctl(fault_fd, UFFDIO_API, &api) != 0) fault_error = errno;
    ::close(fault_fd);
  }

  io_uring_params params{};
  params.flags = IORING_SETUP_R_DISABLED;
  errno = 0;
  const int ring = static_cast<int>(::syscall(SYS_io_uring_setup, 8, &params));
  const int ring_error = ring < 0 ? errno : 0;
  int restriction_error = 0;
  bool restrictions_accepted = false;
  if (ring >= 0) {
    io_uring_restriction restriction{};
    restriction.opcode = IORING_RESTRICTION_SQE_OP;
    restriction.sqe_op = IORING_OP_NOP;
    if (::syscall(SYS_io_uring_register, ring, IORING_REGISTER_RESTRICTIONS, &restriction, 1) == 0)
      restrictions_accepted = true;
    else
      restriction_error = errno;
    ::close(ring);
  }

  std::printf("{\n  \"kernel\": \"%s\", \"architecture\": \"%s\",\n", machine.release, machine.machine);
  std::printf("  \"page_bytes\": %ld, \"read_only_write_trapped\": %s, \"parent_unchanged\": %s,\n",
              page_size, read_only_enforced ? "true" : "false", parent_unchanged ? "true" : "false");
  std::printf("  \"uffd_errno\": %d, \"uffd_features\": %llu,\n", fault_error,
              static_cast<unsigned long long>(api.features));
  std::printf("  \"uffd_missing_shmem_advertised\": %s, \"uffd_minor_shmem_advertised\": %s, \"uffd_wp_shmem_advertised\": %s,\n",
              (api.features & UFFD_FEATURE_MISSING_SHMEM) ? "true" : "false",
              (api.features & UFFD_FEATURE_MINOR_SHMEM) ? "true" : "false",
              (api.features & UFFD_FEATURE_WP_HUGETLBFS_SHMEM) ? "true" : "false");
  std::printf("  \"uring_setup_errno\": %d, \"uring_nop_only_restriction_accepted\": %s, \"uring_restriction_errno\": %d\n}\n",
              ring_error, restrictions_accepted ? "true" : "false", restriction_error);
  return read_only_enforced && parent_unchanged ? 0 : 1;
}
