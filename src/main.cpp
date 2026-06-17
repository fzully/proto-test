#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"

using im::chat::v1::ChatMessage;
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
  assert(parsed.status() == original.status());
  assert(parsed.quote().quoted_message_id() == original.quote().quoted_message_id());
  assert(parsed.quote().content_preview() == original.quote().content_preview());
  assert(parsed.mentioned_user_ids_size() == original.mentioned_user_ids_size());
  assert(parsed.content_case() == ChatMessage::kText);
  assert(parsed.text().body() == original.text().body());

  std::cout << "TestTextMessageRoundTrip passed, serialized size = " << bytes.size()
            << " bytes\n";
}

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
