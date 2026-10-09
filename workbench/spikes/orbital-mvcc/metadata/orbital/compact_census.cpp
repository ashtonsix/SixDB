#include "candidates/candidate_store.h"
#include <cstdio>
#include <type_traits>

template<class T> void record(const char* name) {
  std::printf("{\"type\":\"%s\",\"size\":%zu,\"alignment\":%zu,"
      "\"trivial_copy\":%s,\"movable\":%s,\"trivial_destroy\":%s}\n",
      name, sizeof(T), alignof(T), std::is_trivially_copyable_v<T> ? "true" : "false",
      std::is_move_constructible_v<T> ? "true" : "false",
      std::is_trivially_destructible_v<T> ? "true" : "false");
}

int main() {
  namespace h = compact_history;
  namespace d = compact_descriptor;
  record<compact_version_probe::Version8>("Version8");
  record<compact_version_probe::Version16>("Version16");
  record<compact_version_probe::Version32>("Version32");
  record<h::Memory>("Memory");
  record<h::Store16>("Store");
  record<h::Store16::Scope>("Scope");
  record<h::Store16::Borrowed>("Borrowed");
  record<h::DescriptorOwner>("DescriptorOwner");
  record<h::Change>("Change");
  record<h::Store16::ConversionTarget>("ConversionTarget");
  record<orbital::Result<h::Store16::ConversionTarget>>("Result<ConversionTarget>");
  record<h::Store16::Prepared>("Prepared");
  record<orbital::Result<h::Store16::Prepared>>("Result<Prepared>");
  record<h::Store16::Statistics>("Statistics");
  record<d::Selection>("Selection");
  record<orbital::Result<d::Selection>>("Result<Selection>");
  record<d::View>("View");
  record<orbital::Result<d::View>>("Result<View>");
  record<d::FrozenDescription>("FrozenDescription");
  record<d::Description>("Description");
  record<d::RepresentationSpec>("RepresentationSpec");
  record<d::PartSource>("PartSource");
  record<d::detail::Part>("Part record");
}
