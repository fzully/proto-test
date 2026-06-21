#include "json_nlohmann_fixtures.h"

#include <nlohmann/json.hpp>

namespace json_nlohmann {

namespace {
using nlohmann::json;
}

std::string EncodeTextMessageJson() {
  json j;
  j["messageId"] = "1001";
  j["clientMsgId"] = "client-uuid-abc123";
  j["conversationId"] = "555";
  j["conversationType"] = "CONVERSATION_TYPE_GROUP";
  j["senderId"] = "42";
  j["seq"] = "17";
  j["clientTimestampMs"] = "1750000000000";
  j["serverTimestampMs"] = "1750000000050";
  j["status"] = "MESSAGE_STATUS_SENT";
  j["quotedMessageId"] = "998";
  j["quotedSenderId"] = "7";
  j["contentPreview"] = "原消息预览文本";
  j["mentionedUserIds"] = json::array({"7", "9"});
  j["body"] = "Hello, this is a test message.";
  return j.dump();
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  json j = json::parse(json_text);
  DecodedTextMessage msg;
  msg.message_id = std::stoll(j.at("messageId").get<std::string>());
  msg.client_msg_id = j.at("clientMsgId").get<std::string>();
  msg.conversation_id = std::stoll(j.at("conversationId").get<std::string>());
  msg.conversation_type = j.at("conversationType").get<std::string>();
  msg.sender_id = std::stoll(j.at("senderId").get<std::string>());
  msg.seq = std::stoll(j.at("seq").get<std::string>());
  msg.client_timestamp_ms = std::stoll(j.at("clientTimestampMs").get<std::string>());
  msg.server_timestamp_ms = std::stoll(j.at("serverTimestampMs").get<std::string>());
  msg.status = j.at("status").get<std::string>();
  msg.quoted_message_id = std::stoll(j.at("quotedMessageId").get<std::string>());
  msg.quoted_sender_id = std::stoll(j.at("quotedSenderId").get<std::string>());
  msg.content_preview = j.at("contentPreview").get<std::string>();
  for (const auto& id : j.at("mentionedUserIds")) {
    msg.mentioned_user_ids.push_back(std::stoll(id.get<std::string>()));
  }
  msg.body = j.at("body").get<std::string>();
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  json j;
  j["messageId"] = "2002";
  j["clientMsgId"] = "client-uuid-def456";
  j["conversationId"] = "777";
  j["conversationType"] = "CONVERSATION_TYPE_SINGLE";
  j["senderId"] = "42";
  j["seq"] = "18";
  j["clientTimestampMs"] = "1750000001000";
  j["serverTimestampMs"] = "1750000001050";
  j["status"] = "MESSAGE_STATUS_SENT";
  j["title"] = "群聊的聊天记录";

  json items = json::array();
  json item1;
  item1["messageId"] = "101";
  item1["senderId"] = "7";
  item1["timestampMs"] = "1749999999000";
  item1["body"] = "第一条被转发的消息";
  items.push_back(item1);

  json item2;
  item2["messageId"] = "102";
  item2["senderId"] = "9";
  item2["timestampMs"] = "1749999999500";
  item2["body"] = "第二条被转发的消息";
  items.push_back(item2);

  j["items"] = items;
  return j.dump();
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  json j = json::parse(json_text);
  DecodedMergedForwardMessage msg;
  msg.message_id = std::stoll(j.at("messageId").get<std::string>());
  msg.client_msg_id = j.at("clientMsgId").get<std::string>();
  msg.conversation_id = std::stoll(j.at("conversationId").get<std::string>());
  msg.conversation_type = j.at("conversationType").get<std::string>();
  msg.sender_id = std::stoll(j.at("senderId").get<std::string>());
  msg.seq = std::stoll(j.at("seq").get<std::string>());
  msg.client_timestamp_ms = std::stoll(j.at("clientTimestampMs").get<std::string>());
  msg.server_timestamp_ms = std::stoll(j.at("serverTimestampMs").get<std::string>());
  msg.status = j.at("status").get<std::string>();
  msg.title = j.at("title").get<std::string>();
  for (const auto& item : j.at("items")) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = std::stoll(item.at("messageId").get<std::string>());
    decoded_item.sender_id = std::stoll(item.at("senderId").get<std::string>());
    decoded_item.timestamp_ms = std::stoll(item.at("timestampMs").get<std::string>());
    decoded_item.body = item.at("body").get<std::string>();
    msg.items.push_back(decoded_item);
  }
  return msg;
}

}  // namespace json_nlohmann
