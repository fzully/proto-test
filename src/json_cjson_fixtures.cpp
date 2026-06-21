#include "json_cjson_fixtures.h"

#include <cJSON.h>

#include <cstdlib>

namespace json_cjson {

namespace {

std::string PrintAndDelete(cJSON* root) {
  char* text = cJSON_PrintUnformatted(root);
  std::string result(text);
  free(text);
  cJSON_Delete(root);
  return result;
}

std::int64_t GetInt64(const cJSON* obj, const char* key) {
  return std::strtoll(cJSON_GetObjectItem(obj, key)->valuestring, nullptr, 10);
}

std::string GetStr(const cJSON* obj, const char* key) {
  return cJSON_GetObjectItem(obj, key)->valuestring;
}

}  // namespace

std::string EncodeTextMessageJson() {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "messageId", "1001");
  cJSON_AddStringToObject(root, "clientMsgId", "client-uuid-abc123");
  cJSON_AddStringToObject(root, "conversationId", "555");
  cJSON_AddStringToObject(root, "conversationType", "CONVERSATION_TYPE_GROUP");
  cJSON_AddStringToObject(root, "senderId", "42");
  cJSON_AddStringToObject(root, "seq", "17");
  cJSON_AddStringToObject(root, "clientTimestampMs", "1750000000000");
  cJSON_AddStringToObject(root, "serverTimestampMs", "1750000000050");
  cJSON_AddStringToObject(root, "status", "MESSAGE_STATUS_SENT");
  cJSON_AddStringToObject(root, "quotedMessageId", "998");
  cJSON_AddStringToObject(root, "quotedSenderId", "7");
  cJSON_AddStringToObject(root, "contentPreview", "原消息预览文本");

  cJSON* mentions = cJSON_CreateArray();
  cJSON_AddItemToArray(mentions, cJSON_CreateString("7"));
  cJSON_AddItemToArray(mentions, cJSON_CreateString("9"));
  cJSON_AddItemToObject(root, "mentionedUserIds", mentions);

  cJSON_AddStringToObject(root, "body", "Hello, this is a test message.");

  return PrintAndDelete(root);
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  cJSON* root = cJSON_Parse(json_text.c_str());

  DecodedTextMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.quoted_message_id = GetInt64(root, "quotedMessageId");
  msg.quoted_sender_id = GetInt64(root, "quotedSenderId");
  msg.content_preview = GetStr(root, "contentPreview");

  cJSON* mentions = cJSON_GetObjectItem(root, "mentionedUserIds");
  const int mention_count = cJSON_GetArraySize(mentions);
  for (int i = 0; i < mention_count; ++i) {
    msg.mentioned_user_ids.push_back(
        std::strtoll(cJSON_GetArrayItem(mentions, i)->valuestring, nullptr, 10));
  }

  msg.body = GetStr(root, "body");

  cJSON_Delete(root);
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  cJSON* root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "messageId", "2002");
  cJSON_AddStringToObject(root, "clientMsgId", "client-uuid-def456");
  cJSON_AddStringToObject(root, "conversationId", "777");
  cJSON_AddStringToObject(root, "conversationType", "CONVERSATION_TYPE_SINGLE");
  cJSON_AddStringToObject(root, "senderId", "42");
  cJSON_AddStringToObject(root, "seq", "18");
  cJSON_AddStringToObject(root, "clientTimestampMs", "1750000001000");
  cJSON_AddStringToObject(root, "serverTimestampMs", "1750000001050");
  cJSON_AddStringToObject(root, "status", "MESSAGE_STATUS_SENT");
  cJSON_AddStringToObject(root, "title", "群聊的聊天记录");

  cJSON* items = cJSON_CreateArray();

  cJSON* item1 = cJSON_CreateObject();
  cJSON_AddStringToObject(item1, "messageId", "101");
  cJSON_AddStringToObject(item1, "senderId", "7");
  cJSON_AddStringToObject(item1, "timestampMs", "1749999999000");
  cJSON_AddStringToObject(item1, "body", "第一条被转发的消息");
  cJSON_AddItemToArray(items, item1);

  cJSON* item2 = cJSON_CreateObject();
  cJSON_AddStringToObject(item2, "messageId", "102");
  cJSON_AddStringToObject(item2, "senderId", "9");
  cJSON_AddStringToObject(item2, "timestampMs", "1749999999500");
  cJSON_AddStringToObject(item2, "body", "第二条被转发的消息");
  cJSON_AddItemToArray(items, item2);

  cJSON_AddItemToObject(root, "items", items);

  return PrintAndDelete(root);
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  cJSON* root = cJSON_Parse(json_text.c_str());

  DecodedMergedForwardMessage msg;
  msg.message_id = GetInt64(root, "messageId");
  msg.client_msg_id = GetStr(root, "clientMsgId");
  msg.conversation_id = GetInt64(root, "conversationId");
  msg.conversation_type = GetStr(root, "conversationType");
  msg.sender_id = GetInt64(root, "senderId");
  msg.seq = GetInt64(root, "seq");
  msg.client_timestamp_ms = GetInt64(root, "clientTimestampMs");
  msg.server_timestamp_ms = GetInt64(root, "serverTimestampMs");
  msg.status = GetStr(root, "status");
  msg.title = GetStr(root, "title");

  cJSON* items = cJSON_GetObjectItem(root, "items");
  const int item_count = cJSON_GetArraySize(items);
  for (int i = 0; i < item_count; ++i) {
    cJSON* item = cJSON_GetArrayItem(items, i);
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = GetInt64(item, "messageId");
    decoded_item.sender_id = GetInt64(item, "senderId");
    decoded_item.timestamp_ms = GetInt64(item, "timestampMs");
    decoded_item.body = GetStr(item, "body");
    msg.items.push_back(decoded_item);
  }

  cJSON_Delete(root);
  return msg;
}

}  // namespace json_cjson
