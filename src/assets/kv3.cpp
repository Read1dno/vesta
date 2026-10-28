#include <stdafx.hpp>
#include <assets/kv3.hpp>

#include <lz4.h>
#include <zstd.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace assets::kv3
{

namespace
{

constexpr std::uint32_t k_magic0{0x03564B56}; // legacy text-wrapped KV3, unused by compiled CS2 resources
constexpr std::uint32_t k_kv3_prefix{0x4B563300};

enum class node_type : std::uint8_t
{
    null_value = 1,
    boolean = 2,
    int64 = 3,
    uint64 = 4,
    double_value = 5,
    string = 6,
    binary_blob = 7,
    array_value = 8,
    object_value = 9,
    array_typed = 10,
    int32 = 11,
    uint32 = 12,
    boolean_true = 13,
    boolean_false = 14,
    int64_zero = 15,
    int64_one = 16,
    double_zero = 17,
    double_one = 18,
    float_value = 19,
    int16 = 20,
    uint16 = 21,
    int8 = 22,
    uint8 = 23,
    array_type_byte_length = 24,
    array_type_auxiliary_buffer = 25,
};

class cursor
{
public:
    cursor() = default;
    cursor(const std::uint8_t* data, std::size_t start, std::size_t end)
        : m_data(data)
        , m_pos(start)
        , m_end(end)
    {
    }

    [[nodiscard]] const std::uint8_t* read(std::size_t n)
    {
        if (m_pos + n > m_end)
        {
            throw decode_error(failure::truncated, "kv3: cursor overrun");
        }

        const auto* p = m_data + m_pos;
        m_pos += n;
        return p;
    }

    template<typename T>
    [[nodiscard]] T read_pod()
    {
        T value{};
        std::memcpy(&value, read(sizeof(T)), sizeof(T));
        return value;
    }

    [[nodiscard]] std::size_t remaining() const { return m_end - m_pos; }
    [[nodiscard]] std::size_t pos() const { return m_pos; }

private:
    const std::uint8_t* m_data{};
    std::size_t m_pos{};
    std::size_t m_end{};
};

struct buffers
{
    cursor bytes1{};
    cursor bytes2{};
    cursor bytes4{};
    cursor bytes8{};
};

struct context
{
    int version{};
    cursor types{};
    cursor object_lengths{};
    cursor binary_blobs{};
    cursor binary_blob_lengths{};
    std::vector<std::string> strings{};
    buffers* buffer{};    // main pools: v5 buffer 2, v4 the only buffer
    buffers* auxiliary{}; // v5 buffer 1: primitive elements of ARRAY_TYPE_AUXILIARY_BUFFER; v4 the same as `buffer`
};

[[nodiscard]] std::size_t align_up(std::size_t offset, std::size_t alignment)
{
    --alignment;
    return (offset + alignment) & ~alignment;
}

[[nodiscard]] std::string read_null_term_utf8(cursor& c)
{
    const auto* start = c.read(0); // peek without consuming
    std::size_t len = 0;
    while (len < c.remaining() && start[len] != 0)
    {
        ++len;
    }

    if (len >= c.remaining())
    {
        throw decode_error(failure::string_table, "kv3: unterminated string in string table");
    }

    std::string result(reinterpret_cast<const char*>(start), len);
    static_cast<void>(c.read(len + 1));
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> lz4_chain_decode(cursor& reader, const std::vector<std::uint16_t>& chunk_sizes,
                                                         const std::int32_t* blob_lengths, std::size_t blob_count,
                                                         std::uint16_t frame_size, std::uint32_t total_size)
{
    std::vector<std::uint8_t> out{};
    out.reserve(total_size);

    LZ4_streamDecode_t stream{};
    LZ4_setStreamDecode(&stream, nullptr, 0);

    std::size_t chunk_index = 0;
    for (std::size_t bi = 0; bi < blob_count; ++bi)
    {
        auto remaining = static_cast<std::uint32_t>(blob_lengths[bi]);
        while (remaining > 0)
        {
            if (chunk_index >= chunk_sizes.size())
            {
                throw decode_error(failure::blob_table, "kv3: LZ4 blob chunk table underrun");
            }

            const auto comp_len = chunk_sizes[chunk_index++];
            const auto want = std::min<std::uint32_t>(frame_size, remaining);
            const auto* compressed = reader.read(comp_len);

            const auto base = out.size();
            out.resize(base + want);

            const auto written = LZ4_decompress_safe_continue(&stream, reinterpret_cast<const char*>(compressed),
                                                              reinterpret_cast<char*>(out.data() + base),
                                                              static_cast<int>(comp_len), static_cast<int>(want));

            if (written < 0 || static_cast<std::uint32_t>(written) != want)
            {
                throw decode_error(failure::decompression, "kv3: chained LZ4 block decode failed");
            }

            remaining -= want;
        }
    }

    if (out.size() != total_size)
    {
        throw decode_error(failure::decompression, "kv3: binary blob chain size mismatch");
    }

    return out;
}

[[nodiscard]] std::vector<std::uint8_t> lz4_block_decode(const std::uint8_t* compressed, std::size_t compressed_size,
                                                         std::size_t uncompressed_size)
{
    std::vector<std::uint8_t> out(uncompressed_size);

    const auto written =
        LZ4_decompress_safe(reinterpret_cast<const char*>(compressed), reinterpret_cast<char*>(out.data()),
                            static_cast<int>(compressed_size), static_cast<int>(uncompressed_size));

    if (written < 0 || static_cast<std::size_t>(written) != uncompressed_size)
    {
        throw decode_error(failure::decompression, "kv3: LZ4 block decode failed or size mismatch");
    }

    return out;
}

[[nodiscard]] std::vector<std::uint8_t> zstd_decode(const std::uint8_t* compressed, std::size_t compressed_size,
                                                    std::size_t uncompressed_size)
{
    std::vector<std::uint8_t> out(uncompressed_size);

    const auto written = ZSTD_decompress(out.data(), uncompressed_size, compressed, compressed_size);

    if (ZSTD_isError(written) || written != uncompressed_size)
    {
        throw decode_error(failure::decompression, "kv3: ZSTD decode failed or size mismatch");
    }

    return out;
}

[[nodiscard]] std::pair<std::uint8_t, std::uint8_t> read_type(context& ctx)
{
    auto databyte = ctx.types.read(1)[0];
    std::uint8_t flag{0};

    if (ctx.version >= 3)
    {
        if (databyte & 0x80)
            flag = ctx.types.read(1)[0];
        // No known writer sets 0x40; readers skip the byte that follows it.
        if (databyte & 0x40)
            static_cast<void>(ctx.types.read(1));
        databyte &= 0x3F;
    }
    else if (databyte & 0x80)
    {
        databyte &= 0x7F;
        flag = ctx.types.read(1)[0];
    }

    return {databyte, flag};
}

// Negative and out-of-range ids (-1 is the empty string) read as an empty string, as the game does.
[[nodiscard]] std::string string_at(const context& ctx, const std::int32_t id)
{
    const auto index = static_cast<std::size_t>(static_cast<std::uint32_t>(id));
    return id >= 0 && index < ctx.strings.size() ? ctx.strings[index] : std::string{};
}

[[nodiscard]] object read_value(context& ctx, std::uint8_t datatype, buffers& lane);

void parse_member(context& ctx, object& parent)
{
    const auto [datatype, flag] = read_type(ctx);
    (void)flag; // resource/subclass/panorama annotations; irrelevant to geometry extraction

    if (parent.is_array())
    {
        std::get<array>(parent.m_value).push_back(read_value(ctx, datatype, *ctx.buffer));
        return;
    }

    auto name = string_at(ctx, ctx.buffer->bytes4.read_pod<std::int32_t>());
    // A repeated member name replaces the earlier value.
    std::get<dict>(parent.m_value).insert_or_assign(std::move(name), read_value(ctx, datatype, *ctx.buffer));
}

// Primitive values come from `lane`, which is the auxiliary buffer for elements of ARRAY_TYPE_AUXILIARY_BUFFER.
// Strings, blobs and container lengths always come from the main buffer.
object read_value(context& ctx, std::uint8_t raw_type, buffers& lane)
{
    const auto type = static_cast<node_type>(raw_type);
    auto& buf = *ctx.buffer;

    switch (type)
    {
        case node_type::null_value:
            return object{};
        case node_type::boolean_true:
            return object{true};
        case node_type::boolean_false:
            return object{false};
        case node_type::int64_zero:
            return object{std::int64_t{0}};
        case node_type::int64_one:
            return object{std::int64_t{1}};
        case node_type::double_zero:
            return object{0.0};
        case node_type::double_one:
            return object{1.0};

        case node_type::boolean:
            return object{lane.bytes1.read(1)[0] != 0};
        case node_type::int8:
            return object{static_cast<std::int64_t>(static_cast<std::int8_t>(lane.bytes1.read(1)[0]))};
        case node_type::uint8:
            return object{static_cast<std::uint64_t>(lane.bytes1.read(1)[0])};

        case node_type::int16:
            return object{static_cast<std::int64_t>(lane.bytes2.read_pod<std::int16_t>())};
        case node_type::uint16:
            return object{static_cast<std::uint64_t>(lane.bytes2.read_pod<std::uint16_t>())};

        case node_type::int32:
            return object{static_cast<std::int64_t>(lane.bytes4.read_pod<std::int32_t>())};
        case node_type::uint32:
            return object{static_cast<std::uint64_t>(lane.bytes4.read_pod<std::uint32_t>())};
        case node_type::float_value:
            return object{static_cast<double>(lane.bytes4.read_pod<float>())};

        case node_type::int64:
            return object{lane.bytes8.read_pod<std::int64_t>()};
        case node_type::uint64:
            return object{lane.bytes8.read_pod<std::uint64_t>()};
        case node_type::double_value:
            return object{lane.bytes8.read_pod<double>()};

        case node_type::string:
        {
            return object{string_at(ctx, buf.bytes4.read_pod<std::int32_t>())};
        }

        case node_type::binary_blob:
        {
            if (ctx.version < 2)
            {
                const auto length = buf.bytes4.read_pod<std::int32_t>();
                if (length <= 0)
                    return object{std::vector<std::uint8_t>{}};
                const auto* p = buf.bytes1.read(static_cast<std::size_t>(length));
                return object{std::vector<std::uint8_t>(p, p + length)};
            }

            const auto length = ctx.binary_blob_lengths.read_pod<std::int32_t>();
            if (length <= 0)
                return object{std::vector<std::uint8_t>{}};
            const auto* p = ctx.binary_blobs.read(static_cast<std::size_t>(length));
            return object{std::vector<std::uint8_t>(p, p + length)};
        }

        case node_type::array_value:
        {
            const auto length = buf.bytes4.read_pod<std::int32_t>();
            object result{array{}};
            auto& arr = std::get<array>(result.m_value);
            arr.reserve(static_cast<std::size_t>(std::max(length, 0)));
            for (std::int32_t i = 0; i < length; ++i)
            {
                parse_member(ctx, result);
            }
            return result;
        }

        case node_type::array_typed:
        case node_type::array_type_byte_length:
        case node_type::array_type_auxiliary_buffer:
        {
            const std::int32_t length = type == node_type::array_typed
                                            ? buf.bytes4.read_pod<std::int32_t>()
                                            : static_cast<std::int32_t>(buf.bytes1.read(1)[0]);
            const auto [sub_type, sub_flag] = read_type(ctx);
            (void)sub_flag;
            auto& element_lane = type == node_type::array_type_auxiliary_buffer ? *ctx.auxiliary : buf;

            object result{array{}};
            auto& arr = std::get<array>(result.m_value);
            arr.reserve(static_cast<std::size_t>(std::max(length, 0)));
            for (std::int32_t i = 0; i < length; ++i)
            {
                arr.push_back(read_value(ctx, sub_type, element_lane));
            }
            return result;
        }

        case node_type::object_value:
        {
            const auto length =
                ctx.version >= 5 ? ctx.object_lengths.read_pod<std::int32_t>() : buf.bytes4.read_pod<std::int32_t>();

            object result{dict{}};
            for (std::int32_t i = 0; i < length; ++i)
            {
                parse_member(ctx, result);
            }
            return result;
        }

        default:
            throw decode_error(failure::unknown_node_type, "kv3: unknown node type " + std::to_string(raw_type));
    }
}

} // namespace

const object* object::find(const std::string& key) const
{
    const auto* d = std::get_if<dict>(&m_value);
    if (!d)
    {
        return nullptr;
    }

    const auto it = d->find(key);
    return it == d->end() ? nullptr : &it->second;
}

const object* object::at(std::size_t index) const
{
    const auto* a = std::get_if<array>(&m_value);
    if (!a || index >= a->size())
    {
        return nullptr;
    }

    return &(*a)[index];
}

namespace
{
const object k_null_object{};
} // namespace

const object& object::get(const std::string& key) const
{
    const auto* found = find(key);
    return found ? *found : k_null_object;
}

const object& object::get(const std::size_t index) const
{
    const auto* found = at(index);
    return found ? *found : k_null_object;
}

std::size_t object::size() const
{
    if (const auto* a = std::get_if<array>(&m_value))
        return a->size();
    if (const auto* d = std::get_if<dict>(&m_value))
        return d->size();
    return 0;
}

std::string object::as_string() const
{
    if (const auto* s = std::get_if<std::string>(&m_value))
        return *s;
    return {};
}

double object::as_double() const
{
    if (const auto* v = std::get_if<double>(&m_value))
        return *v;
    if (const auto* v = std::get_if<std::int64_t>(&m_value))
        return static_cast<double>(*v);
    if (const auto* v = std::get_if<std::uint64_t>(&m_value))
        return static_cast<double>(*v);
    return 0.0;
}

std::int64_t object::as_int() const
{
    if (const auto* v = std::get_if<std::int64_t>(&m_value))
        return *v;
    if (const auto* v = std::get_if<std::uint64_t>(&m_value))
        return static_cast<std::int64_t>(*v);
    if (const auto* v = std::get_if<double>(&m_value))
        return static_cast<std::int64_t>(*v);
    return 0;
}

bool object::as_bool() const
{
    if (const auto* v = std::get_if<bool>(&m_value))
        return *v;
    return false;
}

bool object::as_float3(float out[3]) const
{
    const auto* a = std::get_if<array>(&m_value);
    if (!a || a->size() != 3)
    {
        return false;
    }

    for (int i = 0; i < 3; ++i)
        out[i] = static_cast<float>((*a)[i].as_double());
    return true;
}

bool object::as_float4(float out[4]) const
{
    const auto* a = std::get_if<array>(&m_value);
    if (!a || a->size() != 4)
    {
        return false;
    }

    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<float>((*a)[i].as_double());
    return true;
}

namespace
{
constexpr std::uint32_t k_trailer{0xFFEEDD00};

enum class compression : std::uint32_t
{
    none = 0,
    lz4 = 1,
    zstd = 2
};

[[nodiscard]] std::size_t checked_count(const std::int32_t value, const char* what)
{
    if (value < 0)
        throw decode_error(failure::invalid_header, std::string{"kv3: negative "} + what);
    return static_cast<std::size_t>(value);
}

// The fixed header after the magic. Version 5 splits the values into two buffers and adds their sizes and pool
// counts; version 4 has one buffer.
struct header
{
    int version{};
    std::array<std::uint8_t, 16> format_guid{};
    compression method{};
    std::uint16_t frame_size{}; // LZ4 blob frame size
    std::size_t bytes1{};
    std::size_t bytes2{};
    std::size_t bytes4{}; // also holds the string count, so never empty
    std::size_t bytes8{};
    std::size_t types{};
    std::size_t uncompressed_total{};
    std::size_t compressed_total{};
    std::size_t blob_count{};
    std::uint32_t blob_bytes{};
    // Version 5 only.
    std::size_t buffer1_uncompressed{};
    std::size_t buffer1_compressed{};
    std::size_t buffer2_uncompressed{};
    std::size_t buffer2_compressed{};
    std::size_t buffer2_bytes1{};
    std::size_t buffer2_bytes2{};
    std::size_t buffer2_bytes4{};
    std::size_t buffer2_bytes8{};
    std::size_t buffer2_objects{};
};

[[nodiscard]] header read_header(cursor& input, const int version)
{
    header h{.version = version};
    std::memcpy(h.format_guid.data(), input.read(h.format_guid.size()), h.format_guid.size());
    const auto method = input.read_pod<std::uint32_t>();
    if (method > static_cast<std::uint32_t>(compression::zstd))
        throw decode_error(failure::unknown_compression, "kv3: unknown compression method " + std::to_string(method));
    h.method = static_cast<compression>(method);
    static_cast<void>(input.read_pod<std::uint16_t>()); // compression dictionary id
    h.frame_size = input.read_pod<std::uint16_t>();
    h.bytes1 = checked_count(input.read_pod<std::int32_t>(), "byte pool size");
    h.bytes4 = checked_count(input.read_pod<std::int32_t>(), "int pool size");
    h.bytes8 = checked_count(input.read_pod<std::int32_t>(), "eight-byte pool size");
    h.types = checked_count(input.read_pod<std::int32_t>(), "type stream size");
    static_cast<void>(input.read_pod<std::uint16_t>()); // object count
    static_cast<void>(input.read_pod<std::uint16_t>()); // array count
    h.uncompressed_total = checked_count(input.read_pod<std::int32_t>(), "uncompressed size");
    // For v5 the per-buffer sizes below supersede it, but the ZSTD blob stream length is derived from it.
    h.compressed_total = checked_count(input.read_pod<std::int32_t>(), "compressed size");
    h.blob_count = checked_count(input.read_pod<std::int32_t>(), "blob count");
    h.blob_bytes = static_cast<std::uint32_t>(checked_count(input.read_pod<std::int32_t>(), "blob stream size"));
    h.bytes2 = checked_count(input.read_pod<std::int32_t>(), "two-byte pool size");
    static_cast<void>(input.read_pod<std::int32_t>()); // size of the block compressed sizes
    if (h.bytes4 == 0)
        throw decode_error(failure::invalid_header, "kv3: empty int pool (it holds the string count)");
    if (version < 5)
        return h;

    h.buffer1_uncompressed = checked_count(input.read_pod<std::int32_t>(), "buffer 1 size");
    h.buffer1_compressed = checked_count(input.read_pod<std::int32_t>(), "buffer 1 compressed size");
    h.buffer2_uncompressed = checked_count(input.read_pod<std::int32_t>(), "buffer 2 size");
    h.buffer2_compressed = checked_count(input.read_pod<std::int32_t>(), "buffer 2 compressed size");
    h.buffer2_bytes1 = checked_count(input.read_pod<std::int32_t>(), "buffer 2 byte pool size");
    h.buffer2_bytes2 = checked_count(input.read_pod<std::int32_t>(), "buffer 2 two-byte pool size");
    h.buffer2_bytes4 = checked_count(input.read_pod<std::int32_t>(), "buffer 2 int pool size");
    h.buffer2_bytes8 = checked_count(input.read_pod<std::int32_t>(), "buffer 2 eight-byte pool size");
    static_cast<void>(input.read_pod<std::int32_t>()); // unknown
    h.buffer2_objects = checked_count(input.read_pod<std::int32_t>(), "buffer 2 object count");
    static_cast<void>(input.read_pod<std::int32_t>()); // buffer 2 array count
    static_cast<void>(input.read_pod<std::int32_t>()); // unknown
    return h;
}

// One self-contained frame of the input (raw, LZ4 or ZSTD) decoded to `uncompressed_size` bytes.
[[nodiscard]] std::vector<std::uint8_t> decode_frame(cursor& input, const compression method,
                                                     const std::size_t compressed_size,
                                                     const std::size_t uncompressed_size)
{
    switch (method)
    {
        case compression::none:
        {
            const auto* raw = input.read(uncompressed_size);
            return {raw, raw + uncompressed_size};
        }
        case compression::lz4:
            return lz4_block_decode(input.read(compressed_size), compressed_size, uncompressed_size);
        case compression::zstd:
            return zstd_decode(input.read(compressed_size), compressed_size, uncompressed_size);
    }
    throw decode_error(failure::unknown_compression, "kv3: unknown compression method");
}

// [offset, offset + length) of a decoded buffer; throws when it does not fit.
[[nodiscard]] cursor slice(const std::vector<std::uint8_t>& storage, const std::size_t offset, const std::size_t length)
{
    if (offset > storage.size() || length > storage.size() - offset)
        throw decode_error(failure::section_out_of_range, "kv3: section exceeds its buffer");
    return cursor(storage.data(), offset, offset + length);
}

struct pool_sizes
{
    std::size_t bytes1{};
    std::size_t bytes2{};
    std::size_t bytes4{};
    std::size_t bytes8{};
};

// Lays out the 1/2/4/8-byte value pools from `offset`, each aligned to its element size, and returns the offset after
// the last one. Before version 5 an empty eight-byte pool is still aligned, so what follows starts on 8 bytes.
[[nodiscard]] std::size_t layout_pools(const std::vector<std::uint8_t>& storage, std::size_t offset,
                                       const pool_sizes& sizes, buffers& pools, const int version)
{
    const auto place = [&](cursor& pool, const std::size_t count, const std::size_t element_size)
    {
        if (count == 0)
            return;
        offset = align_up(offset, element_size);
        pool = slice(storage, offset, count * element_size);
        offset += count * element_size;
    };
    place(pools.bytes1, sizes.bytes1, 1);
    place(pools.bytes2, sizes.bytes2, 2);
    place(pools.bytes4, sizes.bytes4, 4);
    place(pools.bytes8, sizes.bytes8, 8);
    if (sizes.bytes8 == 0 && version < 5)
        offset = align_up(offset, 8);
    return offset;
}

void expect_trailer(const std::uint32_t value, const char* where)
{
    if (value != k_trailer)
        throw decode_error(failure::missing_trailer, std::string{"kv3: missing trailer "} + where);
}

[[nodiscard]] std::uint32_t read_trailer(const std::vector<std::uint8_t>& storage, const std::size_t offset)
{
    return slice(storage, offset, sizeof(std::uint32_t)).read_pod<std::uint32_t>();
}

// Decoded buffers of one document; every cursor in the context points into them.
struct decoded_storage
{
    std::vector<std::uint8_t> buffer1{}; // v5: pools and strings; v4: everything
    std::vector<std::uint8_t> buffer2{}; // v5: object lengths, pools, types and the blob table
    std::vector<std::uint8_t> blob_lengths{};
    std::vector<std::uint8_t> blobs{};
    buffers pools1{};
    buffers pools2{};
};

// Version 5: buffer 1 holds the pools and the string table; buffer 2 the object lengths, its own pools, the type
// stream and the blob table. Returns the blob table (empty when there are no blobs).
[[nodiscard]] cursor decode_v5_buffers(cursor& input, const header& h, decoded_storage& storage, context& ctx)
{
    storage.buffer1 = decode_frame(input, h.method, h.buffer1_compressed, h.buffer1_uncompressed);
    static_cast<void>(
        layout_pools(storage.buffer1, 0, {h.bytes1, h.bytes2, h.bytes4, h.bytes8}, storage.pools1, h.version));
    const auto string_count = checked_count(storage.pools1.bytes4.read_pod<std::int32_t>(), "string count");
    ctx.strings.resize(string_count);
    for (auto& text : ctx.strings)
        text = read_null_term_utf8(storage.pools1.bytes1);

    storage.buffer2 = decode_frame(input, h.method, h.buffer2_compressed, h.buffer2_uncompressed);
    ctx.buffer = &storage.pools2;
    ctx.auxiliary = &storage.pools1;
    const auto lengths_size = h.buffer2_objects * 4;
    ctx.object_lengths = slice(storage.buffer2, 0, lengths_size);
    auto offset = layout_pools(storage.buffer2, lengths_size,
                               {h.buffer2_bytes1, h.buffer2_bytes2, h.buffer2_bytes4, h.buffer2_bytes8}, storage.pools2,
                               h.version);
    ctx.types = slice(storage.buffer2, offset, h.types);
    offset += h.types;
    if (h.blob_count == 0)
    {
        expect_trailer(read_trailer(storage.buffer2, offset), "after the v5 type stream");
        return {};
    }
    return slice(storage.buffer2, offset, storage.buffer2.size() - std::min(offset, storage.buffer2.size()));
}

// Version 4: one buffer with the pools, then a region of the null-terminated strings followed by the type stream,
// then the blob table. Returns the blob table (empty when there are no blobs).
// With ZSTD the buffer and the blob stream are two consecutive frames counted together in the compressed size, so
// they are decompressed as one and split; the blobs are then already in `storage.blobs`.
[[nodiscard]] cursor decode_v4_buffer(cursor& input, const header& h, decoded_storage& storage, context& ctx)
{
    if (h.method == compression::zstd)
    {
        storage.buffer1 = decode_frame(input, h.method, h.compressed_total, h.uncompressed_total + h.blob_bytes);
        const auto blobs_begin = storage.buffer1.begin() + static_cast<std::ptrdiff_t>(h.uncompressed_total);
        storage.blobs.assign(blobs_begin, storage.buffer1.end());
        storage.buffer1.resize(h.uncompressed_total);
    }
    else
    {
        storage.buffer1 = decode_frame(input, h.method, h.compressed_total, h.uncompressed_total);
    }
    ctx.buffer = &storage.pools1;
    ctx.auxiliary = &storage.pools1;
    const auto region_start =
        layout_pools(storage.buffer1, 0, {h.bytes1, h.bytes2, h.bytes4, h.bytes8}, storage.pools1, h.version);
    const auto region_end = region_start + h.types;
    const auto string_count = checked_count(storage.pools1.bytes4.read_pod<std::int32_t>(), "string count");
    ctx.strings.resize(string_count);
    auto region = slice(storage.buffer1, region_start, h.types);
    for (auto& text : ctx.strings)
        text = read_null_term_utf8(region);
    ctx.types = cursor(storage.buffer1.data(), region.pos(), region_end);
    if (h.blob_count == 0)
    {
        expect_trailer(read_trailer(storage.buffer1, region_end), "after the v4 type stream");
        return {};
    }
    return slice(storage.buffer1, region_end, storage.buffer1.size() - std::min(region_end, storage.buffer1.size()));
}

// The binary blobs: their lengths from the blob table, then the blob stream from the input (raw, one linked LZ4 block
// per blob with the compressed chunk sizes in the rest of the table, or one ZSTD frame).
void decode_blobs(cursor& input, const header& h, cursor& blob_table, decoded_storage& storage, context& ctx)
{
    const auto* lengths = blob_table.read(h.blob_count * 4);
    storage.blob_lengths.assign(lengths, lengths + h.blob_count * 4);
    ctx.binary_blob_lengths = cursor(storage.blob_lengths.data(), 0, storage.blob_lengths.size());
    expect_trailer(blob_table.read_pod<std::uint32_t>(), "before the binary blobs");

    switch (h.method)
    {
        case compression::none:
        {
            const auto* raw = input.read(h.blob_bytes);
            storage.blobs.assign(raw, raw + h.blob_bytes);
            break;
        }
        case compression::lz4:
        {
            std::vector<std::uint16_t> chunk_sizes{};
            std::size_t total_compressed{};
            while (blob_table.remaining() >= sizeof(std::uint16_t))
            {
                chunk_sizes.push_back(blob_table.read_pod<std::uint16_t>());
                total_compressed += chunk_sizes.back();
            }
            const auto* compressed = input.read(total_compressed);
            cursor reader(compressed, 0, total_compressed);
            const auto* blob_lengths = reinterpret_cast<const std::int32_t*>(storage.blob_lengths.data());
            storage.blobs =
                lz4_chain_decode(reader, chunk_sizes, blob_lengths, h.blob_count, h.frame_size, h.blob_bytes);
            break;
        }
        case compression::zstd:
        {
            // v4 decoded the blobs together with its buffer (decode_v4_buffer).
            if (h.version < 5)
                break;
            const auto frames = h.buffer1_compressed + h.buffer2_compressed;
            if (frames > h.compressed_total)
                throw decode_error(failure::invalid_header, "kv3: buffer sizes exceed the compressed total");
            const auto compressed_size = h.compressed_total - frames;
            storage.blobs = zstd_decode(input.read(compressed_size), compressed_size, h.blob_bytes);
            break;
        }
    }
    ctx.binary_blobs = cursor(storage.blobs.data(), 0, storage.blobs.size());
    expect_trailer(input.read_pod<std::uint32_t>(), "after the binary blobs");
}

} // namespace

namespace
{
// Everything after the magic. `method` is set as soon as the header names it, for the error code.
[[nodiscard]] document read_document(cursor& input, const int version, std::uint32_t& method)
{
    const auto h = read_header(input, version);
    method = static_cast<std::uint32_t>(h.method);
    context ctx{.version = version};
    decoded_storage storage{};
    auto blob_table =
        version >= 5 ? decode_v5_buffers(input, h, storage, ctx) : decode_v4_buffer(input, h, storage, ctx);
    if (h.blob_count > 0)
        decode_blobs(input, h, blob_table, storage, ctx);

    document doc{};
    doc.format_guid = h.format_guid;
    const auto [root_type, root_flag] = read_type(ctx);
    static_cast<void>(root_flag);
    doc.root = read_value(ctx, root_type, *ctx.buffer);
    return doc;
}
} // namespace

document decode(const std::uint8_t* data, std::size_t size)
{
    cursor input(data, 0, size);
    if (size < sizeof(std::uint32_t))
        throw decode_error(failure::truncated, "kv3: block too small");
    const auto magic = input.read_pod<std::uint32_t>();
    if (magic == k_magic0)
        throw decode_error(failure::unsupported_version, "kv3: legacy text-wrapped KV3 (MAGIC0) is not read");
    if ((magic & 0xFFFFFF00) != k_kv3_prefix)
        throw decode_error(failure::not_kv3, "kv3: not a KV3 block");
    const auto version = static_cast<int>(magic & 0xFF);
    std::uint32_t method{0xFF}; // unknown until the header is read
    try
    {
        if (version != 4 && version != 5)
            throw decode_error(failure::unsupported_version,
                               "kv3: only v4/v5 are implemented (got v" + std::to_string(version) + ")");
        return read_document(input, version, method);
    }
    catch (decode_error& error)
    {
        error.describe_block(version, method);
        throw;
    }
}

} // namespace assets::kv3
