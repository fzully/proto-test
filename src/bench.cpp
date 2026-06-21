#include <string>
#include <vector>

#include <google/protobuf/arena.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "alloc_counter.h"
#include "benchmark/benchmark.h"
#include "chat.pb.h"
#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"
#include "message_fixtures.h"
#include "sbe_message_fixtures.h"
#include "json_cjson_fixtures.h"
#include "json_message_decoded.h"
#include "json_nlohmann_fixtures.h"
#include "json_rapidjson_fixtures.h"
#include "json_yyjson_fixtures.h"

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

void BM_ParseTextArenaResetAllocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
    arena.Reset();
  }
  ResetAllocCounters();
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
    arena.Reset();
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextArenaResetAllocs);

void BM_ParseTextArenaReset10Allocs(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
    arena.Reset();
  }
  ResetAllocCounters();
  int batch = 0;
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
    if (++batch == 10) {
      arena.Reset();
      batch = 0;
    }
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
}
BENCHMARK(BM_ParseTextArenaReset10Allocs);

void BM_SerializeToFreshString(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::size_t last_size = 0;
  for (auto _ : state) {
    std::string bytes;
    static_cast<void>(msg.SerializeToString(&bytes));
    last_size = bytes.size();
  }
  state.counters["bytes"] = static_cast<double>(last_size);
}
BENCHMARK(BM_SerializeToFreshString);

void BM_SerializeToPreallocatedArray(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    bool ok = msg.SerializeToArray(buffer.data(), size);
    static_cast<void>(ok);
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToPreallocatedArray);

void BM_SerializeToCodedStream(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  const int size = static_cast<int>(msg.ByteSizeLong());
  std::vector<char> buffer(static_cast<std::size_t>(size));
  for (auto _ : state) {
    google::protobuf::io::ArrayOutputStream array_stream(buffer.data(), size);
    google::protobuf::io::CodedOutputStream coded_stream(&array_stream);
    static_cast<void>(msg.SerializeToCodedStream(&coded_stream));
  }
  state.counters["bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_SerializeToCodedStream);

void BM_ConcurrentSerializeText(benchmark::State& state) {
  ChatMessage msg = BuildTextMessage();
  std::string bytes;
  for (auto _ : state) {
    bytes.clear();
    static_cast<void>(msg.SerializeToString(&bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentSerializeText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentParseText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));
  for (auto _ : state) {
    ChatMessage parsed;
    static_cast<void>(parsed.ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentParseText)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentParseTextArena(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string bytes;
  static_cast<void>(original.SerializeToString(&bytes));

  google::protobuf::Arena arena;
  {
    ChatMessage* warm = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(warm->ParseFromString(bytes));
  }
  for (auto _ : state) {
    ChatMessage* parsed = google::protobuf::Arena::Create<ChatMessage>(&arena);
    static_cast<void>(parsed->ParseFromString(bytes));
  }
  state.counters["bytes"] = static_cast<double>(bytes.size());
}
BENCHMARK(BM_ConcurrentParseTextArena)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ParseTruncatedText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string truncated = full_bytes.substr(0, full_bytes.size() / 2);
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(truncated);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(truncated.size());
}
BENCHMARK(BM_ParseTruncatedText);

void BM_ParseGarbageText(benchmark::State& state) {
  ChatMessage original = BuildTextMessage();
  std::string full_bytes;
  static_cast<void>(original.SerializeToString(&full_bytes));
  const std::string garbage(full_bytes.size(), '\xFF');
  bool last_ok = true;
  for (auto _ : state) {
    ChatMessage parsed;
    last_ok = parsed.ParseFromString(garbage);
  }
  state.counters["parse_ok"] = last_ok ? 1.0 : 0.0;
  state.counters["bytes"] = static_cast<double>(garbage.size());
}
BENCHMARK(BM_ParseGarbageText);

using im_chat_sbe::ConversationType;
using im_chat_sbe::MergedForwardChatMessage;
using im_chat_sbe::MessageHeader;
using im_chat_sbe::MessageStatus;
using im_chat_sbe::TextChatMessage;

void BM_EncodeTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeTextSbe);

void BM_DecodeTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeTextSbe);

void BM_EncodeMergedForwardSbe(benchmark::State& state) {
  char buffer[2048];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMergedForwardSbe);

void BM_DecodeMergedForwardSbe(benchmark::State& state) {
  char buffer[2048];
  const std::size_t len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    MergedForwardChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    auto& items = dec.items();
    while (items.hasNext()) {
      items.next();
      benchmark::DoNotOptimize(items.messageId());
      benchmark::DoNotOptimize(items.senderId());
      benchmark::DoNotOptimize(items.timestampMs());
      benchmark::DoNotOptimize(items.getBody(tmp, sizeof(tmp)));
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getTitle(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMergedForwardSbe);

void BM_EncodeSparseTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeSparseTextSbe);

void BM_DecodeSparseTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeSparseTextSbe);

void BM_EncodeSmallIdSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeSmallIdSbe);

void BM_DecodeSmallIdSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeSmallIdSbe);

void BM_EncodeLargeIdSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1950123456789012345LL);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeLargeIdSbe);

void BM_DecodeLargeIdSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageWithIdSbe(buffer, sizeof(buffer), 1950123456789012345LL);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeLargeIdSbe);

void BM_EncodeMentionsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageWithMentionCountSbe(buffer, sizeof(buffer), n);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMentionsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_DecodeMentionsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  const std::size_t len = EncodeTextMessageWithMentionCountSbe(buffer, sizeof(buffer), n);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMentionsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_EncodeMergedItemsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeMergedForwardMessageWithItemCountSbe(buffer, sizeof(buffer), n);
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_EncodeMergedItemsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_DecodeMergedItemsSbe(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  static char buffer[1 << 16];
  const std::size_t len = EncodeMergedForwardMessageWithItemCountSbe(buffer, sizeof(buffer), n);
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    MergedForwardChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    auto& items = dec.items();
    while (items.hasNext()) {
      items.next();
      benchmark::DoNotOptimize(items.messageId());
      benchmark::DoNotOptimize(items.senderId());
      benchmark::DoNotOptimize(items.timestampMs());
      benchmark::DoNotOptimize(items.getBody(tmp, sizeof(tmp)));
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getTitle(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeMergedItemsSbe)->Arg(1)->Arg(10)->Arg(100)->Arg(1000);

void BM_DecodeTextHeapAllocsSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  ResetAllocCounters();
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["allocs_per_iter"] =
      static_cast<double>(GetAllocCount()) / static_cast<double>(state.iterations());
  state.counters["bytes_per_iter"] =
      static_cast<double>(GetAllocBytes()) / static_cast<double>(state.iterations());
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_DecodeTextHeapAllocsSbe);

void BM_ConcurrentEncodeTextSbe(benchmark::State& state) {
  char buffer[512];
  std::size_t len = 0;
  for (auto _ : state) {
    len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_ConcurrentEncodeTextSbe)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_ConcurrentDecodeTextSbe(benchmark::State& state) {
  char buffer[512];
  const std::size_t len = EncodeTextMessageSbe(buffer, sizeof(buffer));
  char tmp[256];
  for (auto _ : state) {
    MessageHeader hdr;
    hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
    TextChatMessage dec;
    dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));
    benchmark::DoNotOptimize(dec.messageId());
    benchmark::DoNotOptimize(dec.conversationId());
    benchmark::DoNotOptimize(dec.conversationType());
    benchmark::DoNotOptimize(dec.senderId());
    benchmark::DoNotOptimize(dec.seq());
    benchmark::DoNotOptimize(dec.clientTimestampMs());
    benchmark::DoNotOptimize(dec.serverTimestampMs());
    benchmark::DoNotOptimize(dec.status());
    benchmark::DoNotOptimize(dec.quotedMessageId());
    benchmark::DoNotOptimize(dec.quotedSenderId());
    auto& mentions = dec.mentionedUserIds();
    while (mentions.hasNext()) {
      benchmark::DoNotOptimize(mentions.next().userId());
    }
    benchmark::DoNotOptimize(dec.getClientMsgId(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getContentPreview(tmp, sizeof(tmp)));
    benchmark::DoNotOptimize(dec.getBody(tmp, sizeof(tmp)));
  }
  state.counters["bytes"] = static_cast<double>(len);
}
BENCHMARK(BM_ConcurrentDecodeTextSbe)->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(16)->Threads(20);

void BM_EncodeTextJsonNlohmann(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_nlohmann::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonNlohmann);

void BM_DecodeTextJsonNlohmann(benchmark::State& state) {
  const std::string json_text = json_nlohmann::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_nlohmann::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonNlohmann);

void BM_EncodeMergedForwardJsonNlohmann(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_nlohmann::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonNlohmann);

void BM_DecodeMergedForwardJsonNlohmann(benchmark::State& state) {
  const std::string json_text = json_nlohmann::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_nlohmann::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonNlohmann);

void BM_EncodeTextJsonRapidjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_rapidjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonRapidjson);

void BM_DecodeTextJsonRapidjson(benchmark::State& state) {
  const std::string json_text = json_rapidjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_rapidjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonRapidjson);

void BM_EncodeMergedForwardJsonRapidjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_rapidjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonRapidjson);

void BM_DecodeMergedForwardJsonRapidjson(benchmark::State& state) {
  const std::string json_text = json_rapidjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_rapidjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonRapidjson);

void BM_EncodeTextJsonYyjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_yyjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonYyjson);

void BM_DecodeTextJsonYyjson(benchmark::State& state) {
  const std::string json_text = json_yyjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_yyjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonYyjson);

void BM_EncodeMergedForwardJsonYyjson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_yyjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonYyjson);

void BM_DecodeMergedForwardJsonYyjson(benchmark::State& state) {
  const std::string json_text = json_yyjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_yyjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonYyjson);

void BM_EncodeTextJsonCJson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_cjson::EncodeTextMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeTextJsonCJson);

void BM_DecodeTextJsonCJson(benchmark::State& state) {
  const std::string json_text = json_cjson::EncodeTextMessageJson();
  for (auto _ : state) {
    DecodedTextMessage msg = json_cjson::DecodeTextMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeTextJsonCJson);

void BM_EncodeMergedForwardJsonCJson(benchmark::State& state) {
  std::string json_text;
  for (auto _ : state) {
    json_text = json_cjson::EncodeMergedForwardMessageJson();
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_EncodeMergedForwardJsonCJson);

void BM_DecodeMergedForwardJsonCJson(benchmark::State& state) {
  const std::string json_text = json_cjson::EncodeMergedForwardMessageJson();
  for (auto _ : state) {
    DecodedMergedForwardMessage msg = json_cjson::DecodeMergedForwardMessageJson(json_text);
    benchmark::DoNotOptimize(msg);
  }
  state.counters["bytes"] = static_cast<double>(json_text.size());
}
BENCHMARK(BM_DecodeMergedForwardJsonCJson);

}  // namespace

BENCHMARK_MAIN();
