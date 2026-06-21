#include "json_rapidjson_fixtures.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdlib>

namespace json_rapidjson {

namespace {

using rapidjson::Document;
using rapidjson::StringBuffer;
using rapidjson::Value;
using rapidjson::Writer;

std::int64_t ParseInt64(const Value& v) { return std::strtoll(v.GetString(), nullptr, 10); }

}  // namespace

std::string EncodeTextMessageJson() {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("messageId");
  writer.String("1001");
  writer.Key("clientMsgId");
  writer.String("client-uuid-abc123");
  writer.Key("conversationId");
  writer.String("555");
  writer.Key("conversationType");
  writer.String("CONVERSATION_TYPE_GROUP");
  writer.Key("senderId");
  writer.String("42");
  writer.Key("seq");
  writer.String("17");
  writer.Key("clientTimestampMs");
  writer.String("1750000000000");
  writer.Key("serverTimestampMs");
  writer.String("1750000000050");
  writer.Key("status");
  writer.String("MESSAGE_STATUS_SENT");
  writer.Key("quotedMessageId");
  writer.String("998");
  writer.Key("quotedSenderId");
  writer.String("7");
  writer.Key("contentPreview");
  writer.String("原消息预览文本");
  writer.Key("mentionedUserIds");
  writer.StartArray();
  writer.String("7");
  writer.String("9");
  writer.EndArray();
  writer.Key("body");
  writer.String("Hello, this is a test message.");
  writer.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize());
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  Document doc;
  doc.Parse(json_text.c_str(), json_text.size());
  DecodedTextMessage msg;
  msg.message_id = ParseInt64(doc["messageId"]);
  msg.client_msg_id = doc["clientMsgId"].GetString();
  msg.conversation_id = ParseInt64(doc["conversationId"]);
  msg.conversation_type = doc["conversationType"].GetString();
  msg.sender_id = ParseInt64(doc["senderId"]);
  msg.seq = ParseInt64(doc["seq"]);
  msg.client_timestamp_ms = ParseInt64(doc["clientTimestampMs"]);
  msg.server_timestamp_ms = ParseInt64(doc["serverTimestampMs"]);
  msg.status = doc["status"].GetString();
  msg.quoted_message_id = ParseInt64(doc["quotedMessageId"]);
  msg.quoted_sender_id = ParseInt64(doc["quotedSenderId"]);
  msg.content_preview = doc["contentPreview"].GetString();
  for (const auto& id : doc["mentionedUserIds"].GetArray()) {
    msg.mentioned_user_ids.push_back(ParseInt64(id));
  }
  msg.body = doc["body"].GetString();
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  writer.StartObject();
  writer.Key("messageId");
  writer.String("2002");
  writer.Key("clientMsgId");
  writer.String("client-uuid-def456");
  writer.Key("conversationId");
  writer.String("777");
  writer.Key("conversationType");
  writer.String("CONVERSATION_TYPE_SINGLE");
  writer.Key("senderId");
  writer.String("42");
  writer.Key("seq");
  writer.String("18");
  writer.Key("clientTimestampMs");
  writer.String("1750000001000");
  writer.Key("serverTimestampMs");
  writer.String("1750000001050");
  writer.Key("status");
  writer.String("MESSAGE_STATUS_SENT");
  writer.Key("title");
  writer.String("群聊的聊天记录");
  writer.Key("items");
  writer.StartArray();
  writer.StartObject();
  writer.Key("messageId");
  writer.String("101");
  writer.Key("senderId");
  writer.String("7");
  writer.Key("timestampMs");
  writer.String("1749999999000");
  writer.Key("body");
  writer.String("第一条被转发的消息");
  writer.EndObject();
  writer.StartObject();
  writer.Key("messageId");
  writer.String("102");
  writer.Key("senderId");
  writer.String("9");
  writer.Key("timestampMs");
  writer.String("1749999999500");
  writer.Key("body");
  writer.String("第二条被转发的消息");
  writer.EndObject();
  writer.EndArray();
  writer.EndObject();
  return std::string(buffer.GetString(), buffer.GetSize());
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  Document doc;
  doc.Parse(json_text.c_str(), json_text.size());
  DecodedMergedForwardMessage msg;
  msg.message_id = ParseInt64(doc["messageId"]);
  msg.client_msg_id = doc["clientMsgId"].GetString();
  msg.conversation_id = ParseInt64(doc["conversationId"]);
  msg.conversation_type = doc["conversationType"].GetString();
  msg.sender_id = ParseInt64(doc["senderId"]);
  msg.seq = ParseInt64(doc["seq"]);
  msg.client_timestamp_ms = ParseInt64(doc["clientTimestampMs"]);
  msg.server_timestamp_ms = ParseInt64(doc["serverTimestampMs"]);
  msg.status = doc["status"].GetString();
  msg.title = doc["title"].GetString();
  for (const auto& item : doc["items"].GetArray()) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = ParseInt64(item["messageId"]);
    decoded_item.sender_id = ParseInt64(item["senderId"]);
    decoded_item.timestamp_ms = ParseInt64(item["timestampMs"]);
    decoded_item.body = item["body"].GetString();
    msg.items.push_back(decoded_item);
  }
  return msg;
}

}  // namespace json_rapidjson
