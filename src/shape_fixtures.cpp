#include "shape_fixtures.h"

#include <cstdint>

std::vector<Shape> BuildShapeBatch(int32_t max_consecutive_delta) {
    constexpr int kNumShapes = 50000;
    constexpr int kMaxPointsPerShape = 7;
    constexpr int32_t kMaxCoord = 100000000;

    // The walk can drift at most (kMaxPointsPerShape-1)*max_consecutive_delta
    // from the first point. Keep that margin on all four sides so every vertex
    // stays within [0, kMaxCoord] regardless of per-shape point count.
    const int32_t margin = (kMaxPointsPerShape - 1) * max_consecutive_delta;

    std::vector<Shape> batch;
    batch.reserve(kNumShapes);

    // Deterministic LCG (Knuth multiplier + odd addend).
    uint64_t rng = 0xDEADBEEFCAFEBABEULL;
    auto rand32 = [&]() -> uint32_t {
        rng = rng * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(rng >> 32);
    };

    const uint32_t coord_range = static_cast<uint32_t>(kMaxCoord - 2 * margin);
    const uint32_t delta_range = static_cast<uint32_t>(2 * max_consecutive_delta + 1);

    for (int s = 0; s < kNumShapes; ++s) {
        // First point: random inside the safe margin zone.
        const int32_t x0 = static_cast<int32_t>(rand32() % coord_range) + margin;
        const int32_t y0 = static_cast<int32_t>(rand32() % coord_range) + margin;

        const int points_per_shape = 5 + static_cast<int>(rand32() % 3);

        Shape shape;
        shape.points.reserve(points_per_shape);
        shape.points.push_back({x0, y0});

        // Subsequent points: random walk with step in [-max, +max].
        for (int p = 1; p < points_per_shape; ++p) {
            const int32_t dx = static_cast<int32_t>(rand32() % delta_range) - max_consecutive_delta;
            const int32_t dy = static_cast<int32_t>(rand32() % delta_range) - max_consecutive_delta;
            shape.points.push_back({
                shape.points.back().x + dx,
                shape.points.back().y + dy,
            });
        }
        batch.push_back(std::move(shape));
    }
    return batch;
}
