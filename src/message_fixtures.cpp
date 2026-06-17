#include "message_fixtures.h"

using im::chat::v1::ChatMessage;
using im::chat::v1::ForwardedItem;
using im::chat::v1::MergedForwardContent;
using im::chat::v1::QuoteInfo;

ChatMessage BuildTextMessageWithId(int64_t message_id) {
  ChatMessage msg;
  msg.set_message_id(message_id);
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

ChatMessage BuildTextMessage() { return BuildTextMessageWithId(1001); }

ChatMessage BuildSparseTextMessage() {
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

  msg.mutable_text()->set_body("Hello, this is a test message.");

  return msg;
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
