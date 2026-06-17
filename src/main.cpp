#include <cassert>
#include <iostream>
#include <string>

#include "chat.pb.h"
#include "message_fixtures.h"

using im::chat::v1::ChatMessage;

namespace {

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

}  // namespace

int main() {
  TestTextMessageRoundTrip();
  TestMergedForwardRoundTrip();
  std::cout << "All protobuf round-trip tests passed.\n";
  return 0;
}
