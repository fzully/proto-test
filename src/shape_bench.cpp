#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"
#include "google/protobuf/arena.h"
#include "lz4.h"
#include "shape.pb.h"
#include "shape_fixtures.h"

// ============================================================
// Byte-level helpers shared by all codecs (little-endian / native)
// ============================================================

template <typename T>
static void BufAppend(std::vector<uint8_t>& buf, T v) {
    buf.resize(buf.size() + sizeof(T));
    std::memcpy(buf.data() + buf.size() - sizeof(T), &v, sizeof(T));
}

template <typename T>
static T BufRead(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

// ============================================================
// Raw binary helpers
//
// Wire format (little-endian, native):
//   [uint32 shape_count]
//   per shape:
//     [uint16 point_count]
//     per point: [int32 x][int32 y]
// ============================================================

namespace raw {

static std::vector<uint8_t> Encode(const std::vector<Shape>& shapes) {
    std::vector<uint8_t> buf;
    // 4B header + per-shape: 2B count + 6 * 8B coords
    buf.reserve(4 + shapes.size() * (2 + 6 * 8));
    BufAppend(buf, static_cast<uint32_t>(shapes.size()));
    for (const auto& s : shapes) {
        BufAppend(buf, static_cast<uint16_t>(s.points.size()));
        for (const auto& pt : s.points) {
            BufAppend(buf, pt.x);
            BufAppend(buf, pt.y);
        }
    }
    return buf;
}

static std::vector<Shape> Decode(const uint8_t* data, size_t /*len*/) {
    const uint8_t* p = data;
    const uint32_t shape_count = BufRead<uint32_t>(p); p += 4;
    std::vector<Shape> shapes;
    shapes.reserve(shape_count);
    for (uint32_t i = 0; i < shape_count; ++i) {
        const uint16_t pt_count = BufRead<uint16_t>(p); p += 2;
        Shape s;
        s.points.reserve(pt_count);
        for (uint16_t j = 0; j < pt_count; ++j) {
            const int32_t x = BufRead<int32_t>(p); p += 4;
            const int32_t y = BufRead<int32_t>(p); p += 4;
            s.points.push_back({x, y});
        }
        shapes.push_back(std::move(s));
    }
    return shapes;
}

}  // namespace raw

// ============================================================
// Protobuf helpers
//
// Delta-encode coordinates: first point is absolute (x0, y0),
// subsequent points are stored as (dx, dy) from the previous.
// sint32 packed fields apply zigzag+varint, yielding ~2-3 bytes
// for deltas within the 500_000-unit shape radius.
// ============================================================

namespace proto {

static std::string Encode(const std::vector<Shape>& shapes) {
    shape::v1::ShapeBatch batch;
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

static std::vector<Shape> Decode(const std::string& bytes) {
    shape::v1::ShapeBatch batch;
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

// No delta: store absolute coordinates directly as sint32.
// Absolute x/y are in [0, 100_000_000]; zigzag of 100M = 200M which needs
// 4 varint bytes (vs 1-3 bytes for small deltas). Payload will be larger
// but there is no delta subtraction/accumulation in the hot loop.
static std::string EncodeNoDelta(const std::vector<Shape>& shapes) {
    shape::v1::ShapeBatch batch;
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
    shape::v1::ShapeBatch batch;
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

static std::string EncodeArena(const std::vector<Shape>& shapes,
                               google::protobuf::Arena& arena) {
    arena.Reset();
    auto* batch = google::protobuf::Arena::Create<shape::v1::ShapeBatch>(&arena);
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

static std::vector<Shape> DecodeArena(const std::string& bytes,
                                      google::protobuf::Arena& arena) {
    arena.Reset();
    auto* batch = google::protobuf::Arena::Create<shape::v1::ShapeBatch>(&arena);
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

}  // namespace proto

// ============================================================
// Raw binary + LZ4 helpers
//
// Wire format: [uint32 original_size][lz4 compressed payload]
// The original_size header lets the decoder allocate exactly the
// right output buffer without a two-pass decompress.
// ============================================================

namespace lz4wrap {

static std::vector<uint8_t> Encode(const std::vector<Shape>& shapes) {
    const std::vector<uint8_t> raw = raw::Encode(shapes);
    const uint32_t orig_size = static_cast<uint32_t>(raw.size());
    const int max_dst = LZ4_compressBound(static_cast<int>(orig_size));
    std::vector<uint8_t> out(4 + static_cast<size_t>(max_dst));
    std::memcpy(out.data(), &orig_size, 4);
    const int compressed = LZ4_compress_default(
        reinterpret_cast<const char*>(raw.data()),
        reinterpret_cast<char*>(out.data() + 4),
        static_cast<int>(orig_size),
        max_dst);
    out.resize(4 + static_cast<size_t>(compressed));
    return out;
}

static std::vector<Shape> Decode(const uint8_t* data, size_t len) {
    uint32_t orig_size;
    std::memcpy(&orig_size, data, 4);
    std::vector<uint8_t> raw(orig_size);
    LZ4_decompress_safe(
        reinterpret_cast<const char*>(data + 4),
        reinterpret_cast<char*>(raw.data()),
        static_cast<int>(len - 4),
        static_cast<int>(orig_size));
    return raw::Decode(raw.data(), raw.size());
}

}  // namespace lz4wrap

// ============================================================
// Raw binary (delta) + LZ4 helpers
//
// Same delta scheme as Protobuf (first point absolute, rest are
// deltas from the previous point), but serialized as flat int32
// fields — no varint/zigzag overhead, no proto framing.
// The deltas are small (within +-500_000) so the high bytes of
// each int32 are almost always 0x00 or 0xFF, giving LZ4 strong
// run-length compression that the absolute-coord format lacks.
//
// Wire format (before LZ4): [uint32 shape_count]
//   per shape: [uint16 point_count] [int32 x0][int32 y0]
//              [int32 dx1][int32 dy1] ...
// Outer framing: [uint32 original_size][lz4 compressed payload]
// ============================================================

namespace rawdelta {

static std::vector<uint8_t> EncodePlain(const std::vector<Shape>& shapes) {
    std::vector<uint8_t> buf;
    buf.reserve(4 + shapes.size() * (2 + 6 * 8));
    BufAppend(buf, static_cast<uint32_t>(shapes.size()));
    for (const auto& s : shapes) {
        BufAppend(buf, static_cast<uint16_t>(s.points.size()));
        int32_t px = 0, py = 0;
        for (const auto& pt : s.points) {
            BufAppend(buf, pt.x - px);
            BufAppend(buf, pt.y - py);
            px = pt.x;
            py = pt.y;
        }
    }
    return buf;
}

static std::vector<Shape> DecodePlain(const uint8_t* data, size_t /*len*/) {
    const uint8_t* p = data;
    const uint32_t shape_count = BufRead<uint32_t>(p); p += 4;
    std::vector<Shape> shapes;
    shapes.reserve(shape_count);
    for (uint32_t i = 0; i < shape_count; ++i) {
        const uint16_t pt_count = BufRead<uint16_t>(p); p += 2;
        Shape s;
        s.points.reserve(pt_count);
        int32_t cx = 0, cy = 0;
        for (uint16_t j = 0; j < pt_count; ++j) {
            cx += BufRead<int32_t>(p); p += 4;
            cy += BufRead<int32_t>(p); p += 4;
            s.points.push_back({cx, cy});
        }
        shapes.push_back(std::move(s));
    }
    return shapes;
}

static std::vector<uint8_t> Encode(const std::vector<Shape>& shapes) {
    const std::vector<uint8_t> plain = EncodePlain(shapes);
    const uint32_t orig_size = static_cast<uint32_t>(plain.size());
    const int max_dst = LZ4_compressBound(static_cast<int>(orig_size));
    std::vector<uint8_t> out(4 + static_cast<size_t>(max_dst));
    std::memcpy(out.data(), &orig_size, 4);
    const int compressed = LZ4_compress_default(
        reinterpret_cast<const char*>(plain.data()),
        reinterpret_cast<char*>(out.data() + 4),
        static_cast<int>(orig_size),
        max_dst);
    out.resize(4 + static_cast<size_t>(compressed));
    return out;
}

static std::vector<Shape> Decode(const uint8_t* data, size_t len) {
    uint32_t orig_size;
    std::memcpy(&orig_size, data, 4);
    std::vector<uint8_t> plain(orig_size);
    LZ4_decompress_safe(
        reinterpret_cast<const char*>(data + 4),
        reinterpret_cast<char*>(plain.data()),
        static_cast<int>(len - 4),
        static_cast<int>(orig_size));
    return DecodePlain(plain.data(), plain.size());
}

}  // namespace rawdelta

// ============================================================
// Benchmarks
// ============================================================

namespace {

// ---- Protobuf, no delta (absolute coordinates) ----

void BM_Encode_ProtoNoDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::string out;
    for (auto _ : state) {
        out = proto::EncodeNoDelta(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_ProtoNoDelta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_ProtoNoDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto::EncodeNoDelta(shapes);
    for (auto _ : state) {
        auto result = proto::DecodeNoDelta(encoded);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_ProtoNoDelta)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Protobuf, with delta ----

void BM_Encode_Protobuf(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::string out;
    for (auto _ : state) {
        out = proto::Encode(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_Protobuf)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_Protobuf(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto::Encode(shapes);
    for (auto _ : state) {
        auto result = proto::Decode(encoded);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_Protobuf)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Raw binary ----

void BM_Encode_RawBinary(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::vector<uint8_t> out;
    for (auto _ : state) {
        out = raw::Encode(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_RawBinary)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_RawBinary(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::vector<uint8_t> encoded = raw::Encode(shapes);
    for (auto _ : state) {
        auto result = raw::Decode(encoded.data(), encoded.size());
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_RawBinary)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Raw binary with delta (no compression) ----

void BM_Encode_RawDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::vector<uint8_t> out;
    for (auto _ : state) {
        out = rawdelta::EncodePlain(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_RawDelta)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_RawDelta(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::vector<uint8_t> encoded = rawdelta::EncodePlain(shapes);
    for (auto _ : state) {
        auto result = rawdelta::DecodePlain(encoded.data(), encoded.size());
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_RawDelta)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Raw binary + LZ4 ----

void BM_Encode_RawLZ4(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::vector<uint8_t> out;
    for (auto _ : state) {
        out = lz4wrap::Encode(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_RawLZ4)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_RawLZ4(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::vector<uint8_t> encoded = lz4wrap::Encode(shapes);
    for (auto _ : state) {
        auto result = lz4wrap::Decode(encoded.data(), encoded.size());
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_RawLZ4)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Raw binary (delta) + LZ4 ----

void BM_Encode_RawDeltaLZ4(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::vector<uint8_t> out;
    for (auto _ : state) {
        out = rawdelta::Encode(shapes);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_RawDeltaLZ4)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_RawDeltaLZ4(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::vector<uint8_t> encoded = rawdelta::Encode(shapes);
    for (auto _ : state) {
        auto result = rawdelta::Decode(encoded.data(), encoded.size());
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_RawDeltaLZ4)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Protobuf + Arena (new Arena each iteration) ----
// Arena is created and destroyed inside every loop body.
// Measures: Arena init + sub-alloc for 50K Shape objects + serialize +
//           Arena block release on destruction.

void BM_Encode_ProtoArenaPerIter(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    std::string out;
    for (auto _ : state) {
        google::protobuf::Arena arena;
        out = proto::EncodeArena(shapes, arena);
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_ProtoArenaPerIter)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_ProtoArenaPerIter(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto::Encode(shapes);
    for (auto _ : state) {
        google::protobuf::Arena arena;
        auto result = proto::DecodeArena(encoded, arena);
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_ProtoArenaPerIter)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Protobuf + Arena reuse (Reset between iterations) ----
// Arena is created once outside the loop; each iteration calls Reset()
// which only resets the internal pointer without calling free().
// After the first iteration the arena's block pool is fully warmed up,
// so subsequent sub-allocs are pointer-bump only — no malloc/free in
// the hot path. This is the typical production usage pattern.

void BM_Encode_ProtoArenaReuse(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    google::protobuf::Arena arena;
    std::string out;
    for (auto _ : state) {
        out = proto::EncodeArena(shapes, arena);  // Reset() called inside
        benchmark::DoNotOptimize(out);
    }
    state.counters["encoded_bytes"] = static_cast<double>(out.size());
}
BENCHMARK(BM_Encode_ProtoArenaReuse)->Arg(10000)->Arg(100000)->Arg(500000);

void BM_Decode_ProtoArenaReuse(benchmark::State& state) {
    const std::vector<Shape> shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
    const std::string encoded = proto::Encode(shapes);
    google::protobuf::Arena arena;
    for (auto _ : state) {
        auto result = proto::DecodeArena(encoded, arena);  // Reset() called inside
        benchmark::DoNotOptimize(result);
    }
    state.counters["encoded_bytes"] = static_cast<double>(encoded.size());
}
BENCHMARK(BM_Decode_ProtoArenaReuse)->Arg(10000)->Arg(100000)->Arg(500000);

// ---- Fixture build cost ----

void BM_BuildShapeBatch(benchmark::State& state) {
    for (auto _ : state) {
        auto shapes = BuildShapeBatch(static_cast<int32_t>(state.range(0)));
        benchmark::DoNotOptimize(shapes);
    }
}
BENCHMARK(BM_BuildShapeBatch)->Arg(10000)->Arg(100000)->Arg(500000);

}  // namespace

BENCHMARK_MAIN();
