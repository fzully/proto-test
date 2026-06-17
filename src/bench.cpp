#include <string>

#include <google/protobuf/arena.h>

#include "alloc_counter.h"
#include "benchmark/benchmark.h"
#include "chat.pb.h"
#include "message_fixtures.h"

using im::chat::v1::ChatMessage;

namespace {

void BM_SerializeText(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeText);

void BM_ParseText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseText);

void BM_SerializeMergedForward(benchmark::State& state) {
  ChatMessage msg = BuildMergedForwardMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMergedForward);

void BM_ParseMergedForward(benchmark::State& state) {
  ChatMessage original = BuildMergedForwardMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMergedForward);

void BM_SerializeSparseText(benchmark::State& state) {
  ChatMessage msg = BuildSparseTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeSparseText);

void BM_ParseSparseText(benchmark::State& state) {
  ChatMessage original = BuildSparseTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseSparseText);

void BM_SerializeSmallId(benchmark::State& state) {
  ChatMessage msg = BuildTextMessageWithId(1);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeSmallId);

void BM_ParseSmallId(benchmark::State& state) {
  ChatMessage original = BuildTextMessageWithId(1);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseSmallId);

void BM_SerializeLargeId(benchmark::State& state) {
  ChatMessage msg = BuildTextMessageWithId(1950123456789012345LL);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeLargeId);

void BM_ParseLargeId(benchmark::State& state) {
  ChatMessage original = BuildTextMessageWithId(1950123456789012345LL);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseLargeId);

void BM_SerializeMentions(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage msg = BuildTextMessageWithMentionCount(n);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMentions)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_ParseMentions(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage original = BuildTextMessageWithMentionCount(n);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMentions)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_SerializeMergedItems(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage msg = BuildMergedForwardMessageWithItemCount(n);
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_SerializeMergedItems)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_ParseMergedItems(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  ChatMessage original = BuildMergedForwardMessageWithItemCount(n);
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ParseMergedItems)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_ParseTextHeapAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextHeapAllocs);

void BM_ParseTextArenaAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
  }
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextArenaAllocs);

}  // namespace

BENCHMARK_MAIN();
