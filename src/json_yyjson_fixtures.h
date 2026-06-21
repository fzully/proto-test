#ifndef PROTO_TEST_JSON_YYJSON_FIXTURES_H_
#define PROTO_TEST_JSON_YYJSON_FIXTURES_H_

#include <string>

#include "json_message_decoded.h"

namespace json_yyjson {

std::string EncodeTextMessageJson();
DecodedTextMessage DecodeTextMessageJson(const std::string& json_text);
std::string EncodeMergedForwardMessageJson();
DecodedMergedForwardMessage DecodeMergedForwardMessageJson(const std::string& json_text);

}  // namespace json_yyjson

#endif  // PROTO_TEST_JSON_YYJSON_FIXTURES_H_
