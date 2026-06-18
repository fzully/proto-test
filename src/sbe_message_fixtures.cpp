#include "sbe_message_fixtures.h"

#include <string>

#include "im_chat_sbe/MergedForwardChatMessage.h"
#include "im_chat_sbe/MessageHeader.h"
#include "im_chat_sbe/TextChatMessage.h"

using im_chat_sbe::ConversationType;
using im_chat_sbe::MergedForwardChatMessage;
using im_chat_sbe::MessageHeader;
using im_chat_sbe::MessageStatus;
using im_chat_sbe::TextChatMessage;

std::size_t EncodeTextMessageWithIdSbe(char* buffer, std::size_t capacity, std::int64_t message_id) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(message_id)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(998)
      .quotedSenderId(7);

  auto& mentions = msg.mentionedUserIdsCount(2);
  mentions.next().userId(7);
  mentions.next().userId(9);

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string("原消息预览文本"));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeTextMessageSbe(char* buffer, std::size_t capacity) {
  return EncodeTextMessageWithIdSbe(buffer, capacity, 1001);
}

std::size_t EncodeSparseTextMessageSbe(char* buffer, std::size_t capacity) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(1001)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(TextChatMessage::quotedMessageIdNullValue())
      .quotedSenderId(TextChatMessage::quotedSenderIdNullValue());

  msg.mentionedUserIdsCount(0);

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string(""));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeTextMessageWithMentionCountSbe(char* buffer, std::size_t capacity, int mention_count) {
  TextChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(1001)
      .conversationId(555)
      .conversationType(ConversationType::Value::GROUP)
      .senderId(42)
      .seq(17)
      .clientTimestampMs(1750000000000LL)
      .serverTimestampMs(1750000000050LL)
      .status(MessageStatus::Value::SENT)
      .quotedMessageId(998)
      .quotedSenderId(7);

  auto& mentions = msg.mentionedUserIdsCount(static_cast<std::uint16_t>(mention_count));
  for (int i = 1; i <= mention_count; ++i) {
    mentions.next().userId(i);
  }

  msg.putClientMsgId(std::string("client-uuid-abc123"));
  msg.putContentPreview(std::string("原消息预览文本"));
  msg.putBody(std::string("Hello, this is a test message."));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeMergedForwardMessageWithItemCountSbe(char* buffer, std::size_t capacity, int item_count) {
  MergedForwardChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(2002)
      .conversationId(777)
      .conversationType(ConversationType::Value::SINGLE)
      .senderId(42)
      .seq(18)
      .clientTimestampMs(1750000001000LL)
      .serverTimestampMs(1750000001050LL)
      .status(MessageStatus::Value::SENT);

  auto& items = msg.itemsCount(static_cast<std::uint16_t>(item_count));
  for (int i = 0; i < item_count; ++i) {
    items.next()
        .messageId(101 + i)
        .senderId(7)
        .timestampMs(1749999999000LL + i)
        .putBody(std::string("被转发的消息"));
  }

  msg.putClientMsgId(std::string("client-uuid-def456"));
  msg.putTitle(std::string("群聊的聊天记录"));

  return MessageHeader::encodedLength() + msg.encodedLength();
}

std::size_t EncodeMergedForwardMessageSbe(char* buffer, std::size_t capacity) {
  MergedForwardChatMessage msg;
  msg.wrapAndApplyHeader(buffer, 0, capacity)
      .messageId(2002)
      .conversationId(777)
      .conversationType(ConversationType::Value::SINGLE)
      .senderId(42)
      .seq(18)
      .clientTimestampMs(1750000001000LL)
      .serverTimestampMs(1750000001050LL)
      .status(MessageStatus::Value::SENT);

  auto& items = msg.itemsCount(2);
  items.next().messageId(101).senderId(7).timestampMs(1749999999000LL).putBody(std::string("第一条被转发的消息"));
  items.next().messageId(102).senderId(9).timestampMs(1749999999500LL).putBody(std::string("第二条被转发的消息"));

  msg.putClientMsgId(std::string("client-uuid-def456"));
  msg.putTitle(std::string("群聊的聊天记录"));

  return MessageHeader::encodedLength() + msg.encodedLength();
}
