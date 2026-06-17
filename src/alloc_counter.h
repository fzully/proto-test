#ifndef PROTO_TEST_ALLOC_COUNTER_H_
#define PROTO_TEST_ALLOC_COUNTER_H_

#include <cstdint>

void ResetAllocCounters();
int64_t GetAllocCount();
int64_t GetAllocBytes();

#endif  // PROTO_TEST_ALLOC_COUNTER_H_
