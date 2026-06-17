#include <string>

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

}  // namespace

BENCHMARK_MAIN();
