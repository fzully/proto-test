#include "json_yyjson_fixtures.h"

#include <yyjson.h>

#include <cstdlib>

namespace json_yyjson {

namespace {

std::string WriteAndFree(yyjson_mut_doc* doc) {
  std::size_t len = 0;
  char* json_str = yyjson_mut_write(doc, YYJSON_WRITE_NOFLAG, &len);
  std::string result(json_str, len);
  free(json_str);
  yyjson_mut_doc_free(doc);
  return result;
}

std::int64_t GetInt64(yyjson_val* obj, const char* key) {
  return std::strtoll(yyjson_get_str(yyjson_obj_get(obj, key)), nullptr, 10);
}

std::string GetStr(yyjson_val* obj, const char* key) {
  return yyjson_get_str(yyjson_obj_get(obj, key));
}

}  // namespace

std::string EncodeTextMessageJson() {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "messageId", "1001");
  yyjson_mut_obj_add_str(doc, root, "clientMsgId", "client-uuid-abc123");
  yyjson_mut_obj_add_str(doc, root, "conversationId", "555");
  yyjson_mut_obj_add_str(doc, root, "conversationType", "CONVERSATION_TYPE_GROUP");
  yyjson_mut_obj_add_str(doc, root, "senderId", "42");
  yyjson_mut_obj_add_str(doc, root, "seq", "17");
  yyjson_mut_obj_add_str(doc, root, "clientTimestampMs", "1750000000000");
  yyjson_mut_obj_add_str(doc, root, "serverTimestampMs", "1750000000050");
  yyjson_mut_obj_add_str(doc, root, "status", "MESSAGE_STATUS_SENT");
  yyjson_mut_obj_add_str(doc, root, "quotedMessageId", "998");
  yyjson_mut_obj_add_str(doc, root, "quotedSenderId", "7");
  yyjson_mut_obj_add_str(doc, root, "contentPreview", "原消息预览文本");

  yyjson_mut_val* mentions = yyjson_mut_arr(doc);
  yyjson_mut_arr_add_str(doc, mentions, "7");
  yyjson_mut_arr_add_str(doc, mentions, "9");
  yyjson_mut_obj_add_val(doc, root, "mentionedUserIds", mentions);

  yyjson_mut_obj_add_str(doc, root, "body", "Hello, this is a test message.");

  return WriteAndFree(doc);
}

DecodedTextMessage DecodeTextMessageJson(const std::string& json_text) {
  yyjson_doc* doc = yyjson_read(json_text.data(), json_text.size(), 0);
  yyjson_val* root = yyjson_doc_get_root(doc);

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

  yyjson_val* mentions = yyjson_obj_get(root, "mentionedUserIds");
  std::size_t idx, max;
  yyjson_val* id_val;
  yyjson_arr_foreach(mentions, idx, max, id_val) {
    msg.mentioned_user_ids.push_back(std::strtoll(yyjson_get_str(id_val), nullptr, 10));
  }

  msg.body = GetStr(root, "body");

  yyjson_doc_free(doc);
  return msg;
}

std::string EncodeMergedForwardMessageJson() {
  yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
  yyjson_mut_val* root = yyjson_mut_obj(doc);
  yyjson_mut_doc_set_root(doc, root);

  yyjson_mut_obj_add_str(doc, root, "messageId", "2002");
  yyjson_mut_obj_add_str(doc, root, "clientMsgId", "client-uuid-def456");
  yyjson_mut_obj_add_str(doc, root, "conversationId", "777");
  yyjson_mut_obj_add_str(doc, root, "conversationType", "CONVERSATION_TYPE_SINGLE");
  yyjson_mut_obj_add_str(doc, root, "senderId", "42");
  yyjson_mut_obj_add_str(doc, root, "seq", "18");
  yyjson_mut_obj_add_str(doc, root, "clientTimestampMs", "1750000001000");
  yyjson_mut_obj_add_str(doc, root, "serverTimestampMs", "1750000001050");
  yyjson_mut_obj_add_str(doc, root, "status", "MESSAGE_STATUS_SENT");
  yyjson_mut_obj_add_str(doc, root, "title", "群聊的聊天记录");

  yyjson_mut_val* items = yyjson_mut_arr(doc);

  yyjson_mut_val* item1 = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, item1, "messageId", "101");
  yyjson_mut_obj_add_str(doc, item1, "senderId", "7");
  yyjson_mut_obj_add_str(doc, item1, "timestampMs", "1749999999000");
  yyjson_mut_obj_add_str(doc, item1, "body", "第一条被转发的消息");
  yyjson_mut_arr_add_val(items, item1);

  yyjson_mut_val* item2 = yyjson_mut_obj(doc);
  yyjson_mut_obj_add_str(doc, item2, "messageId", "102");
  yyjson_mut_obj_add_str(doc, item2, "senderId", "9");
  yyjson_mut_obj_add_str(doc, item2, "timestampMs", "1749999999500");
  yyjson_mut_obj_add_str(doc, item2, "body", "第二条被转发的消息");
  yyjson_mut_arr_add_val(items, item2);

  yyjson_mut_obj_add_val(doc, root, "items", items);

  return WriteAndFree(doc);
}

DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text) {
  yyjson_doc* doc = yyjson_read(json_text.data(), json_text.size(), 0);
  yyjson_val* root = yyjson_doc_get_root(doc);

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

  yyjson_val* items = yyjson_obj_get(root, "items");
  std::size_t idx, max;
  yyjson_val* item_val;
  yyjson_arr_foreach(items, idx, max, item_val) {
    DecodedForwardedItem decoded_item;
    decoded_item.message_id = GetInt64(item_val, "messageId");
    decoded_item.sender_id = GetInt64(item_val, "senderId");
    decoded_item.timestamp_ms = GetInt64(item_val, "timestampMs");
    decoded_item.body = GetStr(item_val, "body");
    msg.items.push_back(decoded_item);
  }

  yyjson_doc_free(doc);
  return msg;
}

}  // namespace json_yyjson
