#ifndef PROTO_TEST_SBE_MESSAGE_FIXTURES_H_
#define PROTO_TEST_SBE_MESSAGE_FIXTURES_H_

#include <cstddef>
#include <cstdint>

std::size_t EncodeTextMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeTextMessageWithIdSbe(char* buffer, std::size_t capacity, std::int64_t message_id);
std::size_t EncodeSparseTextMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeTextMessageWithMentionCountSbe(char* buffer, std::size_t capacity, int mention_count);
std::size_t EncodeMergedForwardMessageSbe(char* buffer, std::size_t capacity);
std::size_t EncodeMergedForwardMessageWithItemCountSbe(char* buffer, std::size_t capacity, int item_count);

#endif  // PROTO_TEST_SBE_MESSAGE_FIXTURES_H_
