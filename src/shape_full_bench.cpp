#include <cstdint>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "google/protobuf/arena.h"
#include "shape_fixtures.h"
#include "shape_full.pb.h"

// ============================================================
// Protobuf full-runtime helpers (libprotobuf, optimize_for = SPEED)
// Logic is identical to shape_bench.cpp's proto:: namespace;
// only the generated type (shape_full::v1::ShapeBatch) differs.
// ============================================================

namespace proto_full {

static std::string EncodeDelta(const std::vector<Shape>& shapes) {
    shape_full::v1::ShapeBatch batch;
    batch.mutable_shapes()->Reserve(static_cast<int>(shapes.size()));
    for (const auto& s : shapes) {
        auto* pb = batch.add_shapes();
        int32_t px = 0, py = 0;
        for (const auto& pt : s.points) {
            pb->add_coords(pt.x - px);
            pb->add_coords(pt.y - py);
            px = pt.x;
            py = pt.y;
        }
    }
    std::string out;
    static_cast<void>(batch.SerializeToString(&out));
    return out;
}

static std::vector<Shape> DecodeDelta(const std::string& bytes) {
    shape_full::v1::ShapeBatch batch;
    static_cast<void>(batch.ParseFromString(bytes));
    std::vector<Shape> shapes;
    shapes.reserve(batch.shapes_size());
    for (int i = 0; i < batch.shapes_size(); ++i) {
        const auto& pb = batch.shapes(i);
        const int n = pb.coords_size();
        Shape s;
        s.points.reserve(n / 2);
        int32_t cx = 0, cy = 0;
        for (int j = 0; j + 1 < n; j += 2) {
            cx += pb.coords(j);
            cy += pb.coords(j + 1);
            s.points.push_back({cx, cy});
        }
        shapes.push_back(std::move(s));
    }
    return shapes;
}

static std::string EncodeNoDelta(const std::vector<Shape>& shapes) {
    shape_full::v1::ShapeBatch batch;
    batch.mutable_shapes()->Reserve(static_cast<int>(shapes.size()));
    for (const auto& s : shapes) {
        auto* pb = batch.add_shapes();
        for (const auto& pt : s.points) {
            pb->add_coords(pt.x);
            pb->add_coords(pt.y);
        }
    }
    std::string out;
    static_cast<void>(batch.SerializeToString(&out));
    return out;
}

static std::vector<Shape> DecodeNoDelta(const std::string& bytes) {
    shape_full::v1::ShapeBatch batch;
    static_cast<void>(batch.ParseFromString(bytes));
    std::vector<Shape> shapes;
    shapes.reserve(batch.shapes_size());
    for (int i = 0; i < batch.shapes_size(); ++i) {
        const auto& pb = batch.shapes(i);
        const int n = pb.coords_size();
        Shape s;
        s.points.reserve(n / 2);
        for (int j = 0; j + 1 < n; j += 2) {
            s.points.push_back({pb.coords(j), pb.coords(j + 1)});
        }
        shapes.push_back(std::move(s));
    }
    return shapes;
}

static std::string EncodeDeltaArena(const std::vector<Shape>& shapes,
                                    google::protobuf::Arena& arena) {
    arena.Reset();
    auto* batch = google::protobuf::Arena::Create<shape_full::v1::ShapeBatch>(&arena);
    batch->mutable_shapes()->Reserve(static_cast<int>(shapes.size()));
    for (const auto& s : shapes) {
        auto* pb = batch->add_shapes();
        int32_t px = 0, py = 0;
        for (const auto& pt : s.points) {
            pb->add_coords(pt.x - px);
            pb->add_coords(pt.y - py);
            px = pt.x;
            py = pt.y;
        }
    }
    std::string out;
    static_cast<void>(batch->SerializeToString(&out));
    return out;
}

static std::vector<Shape> DecodeDeltaArena(const std::string& bytes,
                                           google::protobuf::Arena& arena) {
    arena.Reset();
    auto* batch = google::protobuf::Arena::Create<shape_full::v1::ShapeBatch>(&arena);
    static_cast<void>(batch->ParseFromString(bytes));
    std::vector<Shape> shapes;
    shapes.reserve(batch->shapes_size());
    for (int i = 0; i < batch->shapes_size(); ++i) {
        const auto& pb = batch->shapes(i);
        const int n = pb.coords_size();
        Shape s;
        s.points.reserve(n / 2);
        int32_t cx = 0, cy = 0;
        for (int j = 0; j + 1 < n; j += 2) {
            cx += pb.coords(j);
            cy += pb.coords(j + 1);
            s.points.push_back({cx, cy});
        }
        shapes.push_back(std::move(s));
    }
    return shapes;
}

}  // namespace proto_full

// ============================================================
// Benchmarks
// ============================================================

namespace {

void BM_Encode_Full_Delta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::string out;
    for (auto _ : state) {
        out = proto_full::EncodeDelta(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_Full_Delta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_Full_Delta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto_full::EncodeDelta(shapes);
    for (auto _ : state) {
        auto result = proto_full::DecodeDelta(encoded);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_Full_Delta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Encode_Full_NoDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::string out;
    for (auto _ : state) {
        out = proto_full::EncodeNoDelta(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_Full_NoDelta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_Full_NoDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto_full::EncodeNoDelta(shapes);
    for (auto _ : state) {
        auto result = proto_full::DecodeNoDelta(encoded);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_Full_NoDelta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Encode_Full_DeltaArenaReuse(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    google::protobuf::Arena arena;
    std::string out;
    for (auto _ : state) {
        out = proto_full::EncodeDeltaArena(shapes, arena);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_Full_DeltaArenaReuse)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_Full_DeltaArenaReuse(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto_full::EncodeDelta(shapes);
    google::protobuf::Arena arena;
    for (auto _ : state) {
        auto result = proto_full::DecodeDeltaArena(encoded, arena);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_Full_DeltaArenaReuse)->Arg(10000)->Arg(100000)->Arg(500000);

}  // namespace

BENCHMARK_MAIN();
