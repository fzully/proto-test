#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::ForwardedItem;
using im::chat::v1::MergedForwardContent;
using im::chat::v1::QuoteInfo;

namespace {

ChatMessage BuildTextMessage() {
  ChatMessage msg;
  msg.set_message_id(1001);
  msg.set_client_msg_id("client-uuid-abc123");
  msg.set_conversation_id(555);
  msg.set_conversation_type(im::chat::v1::CONVERSATION_TYPE_GROUP);
  msg.set_sender_id(42);
  msg.set_seq(17);
  msg.set_client_timestamp_ms(1750000000000);
  msg.set_server_timestamp_ms(1750000000050);
  msg.set_status(im::chat::v1::MESSAGE_STATUS_SENT);

  QuoteInfo* quote = msg.mutable_quote();
  quote->set_quoted_message_id(998);
  quote->set_quoted_sender_id(7);
  quote->set_content_preview("原消息预览文本");

  msg.add_mentioned_user_ids(7);
  msg.add_mentioned_user_ids(9);

  msg.mutable_text()->set_body("Hello, this is a test message.");

  return msg;
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

ChatMessage BuildMergedForwardMessage() {
  ChatMessage msg;
  msg.set_message_id(2002);
  msg.set_client_msg_id("client-uuid-def456");
  msg.set_conversation_id(777);
  msg.set_conversation_type(im::chat::v1::CONVERSATION_TYPE_SINGLE);
  msg.set_sender_id(42);
  msg.set_seq(18);
  msg.set_client_timestamp_ms(1750000001000);
  msg.set_server_timestamp_ms(1750000001050);
  msg.set_status(im::chat::v1::MESSAGE_STATUS_SENT);

  MergedForwardContent* merged = msg.mutable_merged_forward();
  merged->set_title("群聊的聊天记录");

  ForwardedItem* item1 = merged->add_items();
  item1->set_message_id(101);
  item1->set_sender_id(7);
  item1->set_timestamp_ms(1749999999000);
  item1->mutable_text()->set_body("第一条被转发的消息");

  ForwardedItem* item2 = merged->add_items();
  item2->set_message_id(102);
  item2->set_sender_id(9);
  item2->set_timestamp_ms(1749999999500);
  item2->mutable_text()->set_body("第二条被转发的消息");

  return msg;
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

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
