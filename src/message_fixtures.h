#ifndef PROTO_TEST_MESSAGE_FIXTURES_H_
#define PROTO_TEST_MESSAGE_FIXTURES_H_

#include "chat.pb.h"

im::chat::v1::ChatMessage BuildTextMessage();
im::chat::v1::ChatMessage BuildMergedForwardMessage();

#endif  // PROTO_TEST_MESSAGE_FIXTURES_H_
