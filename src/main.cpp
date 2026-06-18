#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

#include "chat.pb.h"
#include "message_fixtures.h"

#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"
#include "sbe_message_fixtures.h"

using im::chat::v1::ChatMessage;

namespace {

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "SBE check failed: " << message << "\n";
    std::abort();
  }
}

void TestTextMessageRoundTrip() {
  ChatMessage original = BuildTextMessage();

  std::string bytes;
  bool serialized = original.SerializeToString(&bytes);
  assert(serialized);

  ChatMessage parsed;
  bool parsed_ok = parsed.ParseFromString(bytes);
  assert(parsed_ok);

  assert(parsed.message_id() == original.message_id());
  assert(parsed.client_msg_id() == original.client_msg_id());
  assert(parsed.conversation_id() == original.conversation_id());
  assert(parsed.conversation_type() == original.conversation_type());
  assert(parsed.sender_id() == original.sender_id());
  assert(parsed.seq() == original.seq());
  assert(parsed.client_timestamp_ms() == original.client_timestamp_ms());
  assert(parsed.server_timestamp_ms() == original.server_timestamp_ms());
  assert(parsed.status() == original.status());
  assert(parsed.quote().quoted_message_id() == original.quote().quoted_message_id());
  assert(parsed.quote().quoted_sender_id() == original.quote().quoted_sender_id());
  assert(parsed.quote().content_preview() == original.quote().content_preview());
  assert(parsed.mentioned_user_ids_size() == original.mentioned_user_ids_size());
  assert(parsed.mentioned_user_ids(0) == original.mentioned_user_ids(0));
  assert(parsed.mentioned_user_ids(1) == original.mentioned_user_ids(1));
  assert(parsed.content_case() == ChatMessage::kText);
  assert(parsed.text().body() == original.text().body());

  std::cout << "TestTextMessageRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
}

void TestMergedForwardRoundTrip() {
  ChatMessage original = BuildMergedForwardMessage();

  std::string bytes;
  bool serialized = original.SerializeToString(&bytes);
  assert(serialized);

  ChatMessage parsed;
  bool parsed_ok = parsed.ParseFromString(bytes);
  assert(parsed_ok);

  assert(parsed.message_id() == original.message_id());
  assert(parsed.client_msg_id() == original.client_msg_id());
  assert(parsed.conversation_id() == original.conversation_id());
  assert(parsed.conversation_type() == original.conversation_type());
  assert(parsed.sender_id() == original.sender_id());
  assert(parsed.seq() == original.seq());
  assert(parsed.client_timestamp_ms() == original.client_timestamp_ms());
  assert(parsed.server_timestamp_ms() == original.server_timestamp_ms());
  assert(parsed.status() == original.status());

  assert(parsed.content_case() == ChatMessage::kMergedForward);
  assert(parsed.merged_forward().title() == original.merged_forward().title());
  assert(parsed.merged_forward().items_size() == 2);

  assert(parsed.merged_forward().items(0).message_id() ==
         original.merged_forward().items(0).message_id());
  assert(parsed.merged_forward().items(0).sender_id() ==
         original.merged_forward().items(0).sender_id());
  assert(parsed.merged_forward().items(0).timestamp_ms() ==
         original.merged_forward().items(0).timestamp_ms());
  assert(parsed.merged_forward().items(0).text().body() ==
         original.merged_forward().items(0).text().body());

  assert(parsed.merged_forward().items(1).message_id() ==
         original.merged_forward().items(1).message_id());
  assert(parsed.merged_forward().items(1).sender_id() ==
         original.merged_forward().items(1).sender_id());
  assert(parsed.merged_forward().items(1).timestamp_ms() ==
         original.merged_forward().items(1).timestamp_ms());
  assert(parsed.merged_forward().items(1).text().body() ==
         original.merged_forward().items(1).text().body());

  std::cout << "TestMergedForwardRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
}

void TestSbeTextMessageRoundTrip() {
  using im_chat_sbe::ConversationType;
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MessageStatus;
  using im_chat_sbe::TextChatMessage;

  char buffer[512];
  const std::size_t total_len = EncodeTextMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  Check(hdr.templateId() == TextChatMessage::sbeTemplateId(), "templateId mismatch");

  TextChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.messageId() == 1001, "messageId");
  Check(dec.conversationId() == 555, "conversationId");
  Check(dec.conversationType() == ConversationType::Value::GROUP, "conversationType");
  Check(dec.senderId() == 42, "senderId");
  Check(dec.seq() == 17, "seq");
  Check(dec.clientTimestampMs() == 1750000000000LL, "clientTimestampMs");
  Check(dec.serverTimestampMs() == 1750000000050LL, "serverTimestampMs");
  Check(dec.status() == MessageStatus::Value::SENT, "status");
  Check(dec.quotedMessageId() == 998, "quotedMessageId");
  Check(dec.quotedSenderId() == 7, "quotedSenderId");

  auto& mentions = dec.mentionedUserIds();
  Check(mentions.count() == 2, "mentionedUserIds count");
  Check(mentions.hasNext(), "mentionedUserIds[0] hasNext");
  Check(mentions.next().userId() == 7, "mentionedUserIds[0]");
  Check(mentions.hasNext(), "mentionedUserIds[1] hasNext");
  Check(mentions.next().userId() == 9, "mentionedUserIds[1]");
  Check(!mentions.hasNext(), "mentionedUserIds exhausted");

  char tmp[256];
  std::uint64_t n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-abc123", "clientMsgId");
  n = dec.getContentPreview(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "原消息预览文本", "contentPreview");
  n = dec.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "Hello, this is a test message.", "body");

  std::cout << "TestSbeTextMessageRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeSparseTextMessageRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::TextChatMessage;

  char buffer[512];
  const std::size_t total_len = EncodeSparseTextMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  TextChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.quotedMessageId() == TextChatMessage::quotedMessageIdNullValue(), "sparse quotedMessageId null");
  Check(dec.quotedSenderId() == TextChatMessage::quotedSenderIdNullValue(), "sparse quotedSenderId null");

  auto& mentions = dec.mentionedUserIds();
  Check(mentions.count() == 0, "sparse mentionedUserIds count");
  Check(!mentions.hasNext(), "sparse mentionedUserIds empty");

  char tmp[256];
  std::uint64_t n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-abc123", "sparse clientMsgId");
  n = dec.getContentPreview(tmp, sizeof(tmp));
  Check(n == 0, "sparse contentPreview empty");
  n = dec.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "Hello, this is a test message.", "sparse body");

  std::cout << "TestSbeSparseTextMessageRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeMergedForwardRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MergedForwardChatMessage;

  char buffer[2048];
  const std::size_t total_len = EncodeMergedForwardMessageSbe(buffer, sizeof(buffer));

  MessageHeader hdr;
  hdr.wrap(buffer, 0, MessageHeader::sbeSchemaVersion(), sizeof(buffer));
  Check(hdr.templateId() == MergedForwardChatMessage::sbeTemplateId(), "templateId mismatch");

  MergedForwardChatMessage dec;
  dec.wrapForDecode(buffer, MessageHeader::encodedLength(), hdr.blockLength(), hdr.version(), sizeof(buffer));

  Check(dec.messageId() == 2002, "messageId");
  Check(dec.conversationId() == 777, "conversationId");

  auto& items = dec.items();
  Check(items.count() == 2, "items count");

  char tmp[256];
  items.next();
  Check(items.messageId() == 101, "item0 messageId");
  Check(items.senderId() == 7, "item0 senderId");
  Check(items.timestampMs() == 1749999999000LL, "item0 timestampMs");
  std::uint64_t n = items.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "第一条被转发的消息", "item0 body");

  items.next();
  Check(items.messageId() == 102, "item1 messageId");
  n = items.getBody(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "第二条被转发的消息", "item1 body");
  Check(!items.hasNext(), "items exhausted");

  n = dec.getClientMsgId(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "client-uuid-def456", "clientMsgId");
  n = dec.getTitle(tmp, sizeof(tmp));
  Check(std::string(tmp, n) == "群聊的聊天记录", "title");

  std::cout << "TestSbeMergedForwardRoundTrip passed, encoded size = " << total_len << " bytes\n";
}

void TestSbeLargeSweepRoundTrip() {
  using im_chat_sbe::MessageHeader;
  using im_chat_sbe::MergedForwardChatMessage;
  using im_chat_sbe::TextChatMessage;

  // Mentions sweep at n=1000 (validates the uint16 groupSizeEncoding fix).
  static char mention_buf[1 << 16];
  const std::size_t mention_len = EncodeTextMessageWithMentionCountSbe(mention_buf, sizeof(mention_buf), 1000);
  MessageHeader mention_hdr;
  mention_hdr.wrap(mention_buf, 0, MessageHeader::sbeSchemaVersion(), sizeof(mention_buf));
  TextChatMessage mention_dec;
  mention_dec.wrapForDecode(
      mention_buf, MessageHeader::encodedLength(), mention_hdr.blockLength(), mention_hdr.version(),
      sizeof(mention_buf));
  auto& mentions = mention_dec.mentionedUserIds();
  Check(mentions.count() == 1000, "1000-mention count");
  int mention_seen = 0;
  while (mentions.hasNext()) {
    mentions.next();
    ++mention_seen;
  }
  Check(mention_seen == 1000, "1000-mention iteration count");
  std::cout << "TestSbeLargeSweepRoundTrip mentions n=1000 passed, encoded size = " << mention_len << " bytes\n";

  // Merged-forward items sweep at n=1000.
  static char items_buf[1 << 16];
  const std::size_t items_len = EncodeMergedForwardMessageWithItemCountSbe(items_buf, sizeof(items_buf), 1000);
  MessageHeader items_hdr;
  items_hdr.wrap(items_buf, 0, MessageHeader::sbeSchemaVersion(), sizeof(items_buf));
  MergedForwardChatMessage items_dec;
  items_dec.wrapForDecode(
      items_buf, MessageHeader::encodedLength(), items_hdr.blockLength(), items_hdr.version(), sizeof(items_buf));
  auto& items = items_dec.items();
  Check(items.count() == 1000, "1000-item count");
  char tmp[256];
  int items_seen = 0;
  while (items.hasNext()) {
    items.next();
    items.getBody(tmp, sizeof(tmp));
    ++items_seen;
  }
  Check(items_seen == 1000, "1000-item iteration count");
  std::cout << "TestSbeLargeSweepRoundTrip items n=1000 passed, encoded size = " << items_len << " bytes\n";
}

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";

  TestSbeTextMessageRoundTrip();
  TestSbeSparseTextMessageRoundTrip();
  TestSbeMergedForwardRoundTrip();
  TestSbeLargeSweepRoundTrip();
  std::cout << "All SBE round-trip tests passed.\n";
  return 0;
}
