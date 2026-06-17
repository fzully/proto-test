#ifndef PROTO_TEST_MESSAGE_FIXTURES_H_
#define PROTO_TEST_MESSAGE_FIXTURES_H_

#include <cstdint>

#include "chat.pb.h"

im::chat::v1::ChatMessage BuildTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithId(int64_t message_id);
im::chat::v1::ChatMessage BuildSparseTextMessage();
im::chat::v1::ChatMessage BuildTextMessageWithMentionCount(int mention_count);
im::chat::v1::ChatMessage BuildMergedForwardMessage();
im::chat::v1::ChatMessage BuildMergedForwardMessageWithItemCount(int item_count);

#endif  // PROTO_TEST_MESSAGE_FIXTURES_H_
