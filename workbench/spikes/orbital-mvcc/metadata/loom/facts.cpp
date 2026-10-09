#include "probe.h"
#include <loom/hardware.h>
#include <orbital/preparation.h>
#include <type_traits>

// Inspect the real frozen caller types, rather than reproducing their layout.
#include LM_CALLER_SOURCE

namespace loom_metadata {
Word native_word(const object_access_check::NativeBody& body, Word row) {
  return body.reader.get_unchecked(row);
}
Word prepared_inline(const object_access_check::NativeBody& body, Word count) {
  Word sum = 0;
  for (Word row = 0; row < count; ++row) sum += body.reader.get_unchecked(row);
  return sum;
}
}

// LLVM constants supply target-compiled facts, including cross targets.
#define FACT(NAME, ...) \
  using NAME = __VA_ARGS__; \
  extern "C" const std::uint64_t lm_fact_##NAME[] = {sizeof(NAME), alignof(NAME), \
    std::is_trivially_copyable_v<NAME>, std::is_trivially_destructible_v<NAME>, \
    std::is_copy_constructible_v<NAME>, std::is_move_constructible_v<NAME>}
FACT(Progress, orbital::Progress);
FACT(ResultVoid, orbital::Result<void>);
FACT(ObjectId, orbital::ObjectId);
FACT(PartId, orbital::PartId);
FACT(Epoch, orbital::Epoch);
FACT(BufferGeneration, orbital::BufferGeneration);
FACT(ByteRange, orbital::ByteRange);
FACT(Bytes, orbital::Bytes);
FACT(MutableBytes, orbital::MutableBytes);
FACT(WaitLink, orbital::WaitLink);
FACT(Signal, orbital::Signal);
FACT(Message, orbital::worker::Message);
FACT(MessageOps, orbital::worker::MessageOps);
FACT(Lane, orbital::worker::Lane);
FACT(Thread, orbital::worker::Thread);
FACT(Worker, loom::Worker);
FACT(Task, loom::Task);
FACT(PollBudget, loom::PollBudget);
FACT(AllocationRequest, orbital::allocation::AllocationRequest);
FACT(Allocation, orbital::allocation::Allocation);
FACT(Hold, orbital::allocation::Hold);
FACT(ResultAllocation, orbital::Result<orbital::allocation::Allocation>);
FACT(ResultHold, orbital::Result<orbital::allocation::Hold>);
FACT(ReleaseHint, orbital::allocation::ReleaseHint);
FACT(Arena, orbital::allocation::Arena);
FACT(Pool, orbital::allocation::Pool);
FACT(PreparedBytes, orbital::allocation::PreparedBytes);
FACT(Preparation, orbital::allocation::Preparation);
FACT(PreparationOutcome, orbital::allocation::Preparation::Outcome);
FACT(AllocationStatistics, orbital::allocation::Statistics);
FACT(RingCapacity, loom::RingCapacity);
FACT(SpinBudget, loom::SpinBudget);
FACT(RingLayout, loom::detail::RingLayout);
FACT(RingStorage, loom::detail::RingStorage);
FACT(ResultRingStorage, orbital::Result<loom::detail::RingStorage>);
FACT(MemoryDemand, loom::MemoryDemand);
FACT(MemoryDependency, loom::MemoryDependency);
FACT(DemandClass, loom::DemandClass);
FACT(HintProfile, loom::HintProfile);
FACT(HintPolicy, loom::HintPolicy);
FACT(Description, orbital::object::Description);
FACT(RepresentationSpec, orbital::object::RepresentationSpec);
FACT(PartSource, orbital::object::PartSource);
FACT(PartPlan, orbital::object::PartPlan);
FACT(View, orbital::object::View);
FACT(Selection, orbital::object::Selection);
FACT(ResultView, orbital::Result<orbital::object::View>);
FACT(ResultSelection, orbital::Result<orbital::object::Selection>);
FACT(BorrowedContext, orbital::object::BorrowedContext);
FACT(ReadContext, orbital::object::ReadContext);
FACT(Representation, orbital::object::Representation);
FACT(RetainedView, orbital::object::RetainedView);
FACT(RetainedSelection, orbital::object::RetainedSelection);
FACT(ResultRetainedView, orbital::Result<orbital::object::RetainedView>);
FACT(ResultReadContext, orbital::Result<orbital::object::ReadContext>);
FACT(ResultRetainedSelection, orbital::Result<orbital::object::RetainedSelection>);
FACT(Builder, orbital::object::Builder);
FACT(WriteView, orbital::object::WriteView);
FACT(ResultWriteView, orbital::Result<orbital::object::WriteView>);
FACT(ResultMutableBytes, orbital::Result<orbital::MutableBytes>);
FACT(Draft, orbital::object::Draft);
FACT(Prepared, orbital::object::Prepared);
FACT(Store, orbital::object::Store);
FACT(ObjectStatistics, orbital::object::Statistics);
FACT(Compound, object_access_check::Compound);
FACT(PhysicalUse, object_access_check::PhysicalUse);
FACT(DelayedFill, object_access_check::DelayedFill);
FACT(RingFrame, object_access_check::RingFrame);
FACT(NativeBody, object_access_check::NativeBody);
FACT(Stage2Ring, object_access_check::Ring);
FACT(Stage2Slot, object_access_check::Ring::Slot);
FACT(Stage2Arguments, object_access_check::Ring::Arguments);
FACT(TupleLayout, object_access_check::tp::layout);
FACT(TupleReader8, object_access_check::tp::reader<8>);
FACT(TupleView, object_access_check::tp::const_view);
FACT(TupleReadOperation, object_access_check::tp::read_operation<8, const object_access_check::tp::byte>);
FACT(TupleNativeReader, object_access_check::tp::native_reader<8, const object_access_check::tp::byte>);
FACT(Packet8, loom_metadata::Packet8);
FACT(Packet16, loom_metadata::Packet16);
FACT(Packet32, loom_metadata::Packet32);
FACT(ResultPacket8, orbital::Result<loom_metadata::Packet8>);
FACT(ResultPacket16, orbital::Result<loom_metadata::Packet16>);
FACT(ResultPacket32, orbital::Result<loom_metadata::Packet32>);
FACT(CompactResult16, loom_metadata::Result16);
