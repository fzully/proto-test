#ifndef PROTO_TEST_JSON_MESSAGE_DECODED_H_
#define PROTO_TEST_JSON_MESSAGE_DECODED_H_

#include <cstdint>
#include <string>
#include <vector>

struct DecodedTextMessage {
  std::int64_t message_id;
  std::string client_msg_id;
  std::int64_t conversation_id;
  std::string conversation_type;
  std::int64_t sender_id;
  std::int64_t seq;
  std::int64_t client_timestamp_ms;
  std::int64_t server_timestamp_ms;
  std::string status;
  std::int64_t quoted_message_id;
  std::int64_t quoted_sender_id;
  std::string content_preview;
  std::vector<std::int64_t> mentioned_user_ids;
  std::string body;
};

struct DecodedForwardedItem {
  std::int64_t message_id;
  std::int64_t sender_id;
  std::int64_t timestamp_ms;
  std::string body;
};

struct DecodedMergedForwardMessage {
  std::int64_t message_id;
  std::string client_msg_id;
  std::int64_t conversation_id;
  std::string conversation_type;
  std::int64_t sender_id;
  std::int64_t seq;
  std::int64_t client_timestamp_ms;
  std::int64_t server_timestamp_ms;
  std::string status;
  std::string title;
  std::vector<DecodedForwardedItem> items;
};

#endif  // PROTO_TEST_JSON_MESSAGE_DECODED_H_
