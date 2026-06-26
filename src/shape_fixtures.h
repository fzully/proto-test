#ifndef PROTO_TEST_SHAPE_FIXTURES_H_
#define PROTO_TEST_SHAPE_FIXTURES_H_

#include <cstdint>
#include <vector>

struct Point {
    int32_t x;
    int32_t y;
};

struct Shape {
    std::vector<Point> points;
};

// Returns a deterministic batch: 50,000 shapes x 6 points each.
// x, y in [0, 100_000_000]. Each shape is generated as a random walk:
// the first point is placed randomly, and each subsequent point is at most
// max_consecutive_delta away (in both x and y) from the previous one.
// This directly controls the delta seen by encoders like Protobuf/LZ4.
std::vector<Shape> BuildShapeBatch(int32_t max_consecutive_delta);

#endif  // PROTO_TEST_SHAPE_FIXTURES_H_
