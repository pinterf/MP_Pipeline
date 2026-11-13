#include "common.h"
#include "FrameProps.h"

#include <assert.h>
#include <string.h>
#include <stdexcept>
#include <string>

namespace {

inline size_t pad8(size_t n) { return (n + 7) & ~(size_t)7; }

// The core stores the hint of propSetDataH unchecked: anything unknown becomes "unknown".
inline int32_t valid_type_hint(int32_t hint)
{
    return hint == PROPDATATYPEHINT_BINARY || hint == PROPDATATYPEHINT_UTF8 ? hint : PROPDATATYPEHINT_UNKNOWN;
}

// Writes into a buffer, or only counts the bytes when the buffer is null.
class Sink
{
public:
    Sink(uint8_t* dst, size_t capacity) : _dst(dst), _capacity(capacity), _pos(0) {}

    void put(const void* data, size_t size)
    {
        if (_dst)
        {
            if (size > _capacity - _pos)
            {
                throw std::runtime_error("Frame property buffer too small.");
            }
            memcpy(_dst + _pos, data, size);
        }
        _pos += size;
    }
    template<typename T> void put_value(T v) { put(&v, sizeof(v)); }
    void align8()
    {
        static const uint8_t zeros[8] = { 0 };
        size_t padded = pad8(_pos);
        put(zeros, padded - _pos);
    }
    size_t pos() const { return _pos; }
    uint8_t* data() const { return _dst; }

private:
    uint8_t* _dst;
    size_t _capacity;
    size_t _pos;
};

// Bounds-checked reader over a serialized block.
class Source
{
public:
    Source(const uint8_t* src, size_t size) : _src(src), _size(size), _pos(0) {}

    const uint8_t* take(size_t size)
    {
        if (size > _size - _pos)
        {
            throw std::runtime_error("Malformed frame property block (truncated).");
        }
        const uint8_t* p = _src + _pos;
        _pos += size;
        return p;
    }
    // count elements of elem_size bytes; checked by division, so a corrupted count can't wrap around in 32 bit
    const uint8_t* take_array(uint32_t count, size_t elem_size)
    {
        if ((size_t)count > (_size - _pos) / elem_size)
        {
            throw std::runtime_error("Malformed frame property block (truncated).");
        }
        return take((size_t)count * elem_size);
    }
    template<typename T> T get_value()
    {
        T v;
        memcpy(&v, take(sizeof(v)), sizeof(v));
        return v;
    }
    void align8() { take(pad8(_pos) - _pos); }
    size_t pos() const { return _pos; }

private:
    const uint8_t* _src;
    size_t _size;
    size_t _pos;
};

// Same rule as the AviSynth+ core (isValidVSMapKey). Checked before any propSet call, because
// older cores (e.g. 3.7.5) throw from a noexcept function on an invalid key, terminating the process.
bool is_valid_key(const std::string& key)
{
    if (key.empty())
    {
        return false;
    }
    for (size_t i = 0; i < key.size(); i++)
    {
        const char c = key[i];
        const bool alpha_underscore = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        const bool digit = c >= '0' && c <= '9';
        if (!alpha_underscore && !(i > 0 && digit))
        {
            return false;
        }
    }
    return true;
}

void check_int_size(size_t value, const char* what)
{
    if (value > 0x7FFFFFFF)
    {
        throw std::runtime_error(std::string("Frame property too large: ") + what);
    }
}

} // namespace

// Interface versions, checked against the AviSynth+ history (vtable order unchanged since):
//   V8 (3.6.0): getFramePropsRO/RW, propNumKeys, propGetKey, propGetType, propNumElements,
//       propGetIntArray, propGetFloatArray, propGetData, propGetDataSize, propDeleteKey,
//       propSetIntArray, propSetFloatArray, propSetData. All of them came in the same commit,
//       before the interface was bumped to 8.
//   V11 (3.7.4): propGetDataTypeHint, propSetDataH; only called when has_type_hint.
// Nothing else of the property API may be used here without a new capability flag.
FramePropsCaps detect_frame_props_caps(IScriptEnvironment* env)
{
    FramePropsCaps caps = { false, false };
    try
    {
        env->CheckVersion(8);
        caps.has_props = true;
    } catch (const AvisynthError&) {
    }
    if (caps.has_props)
    {
        try
        {
            env->CheckVersion(11);
            caps.has_type_hint = true;
        } catch (const AvisynthError&) {
        }
    }
    return caps;
}

size_t serialize_frame_props(IScriptEnvironment* env, const FramePropsCaps& caps, const PVideoFrame& frame, uint8_t* dst, size_t dst_capacity)
{
    if (!caps.has_props || !frame)
    {
        return 0;
    }
    assert(((uintptr_t)dst & 7) == 0);

    const AVSMap* map = env->getFramePropsRO(frame);
    const int num_keys = env->propNumKeys(map);
    if (num_keys <= 0)
    {
        return 0;
    }

    Sink out(dst, dst_capacity);
    // header, total_size/key_count/skipped_count are patched at the end
    out.put_value<uint32_t>(FRAMEPROPS_MAGIC);
    out.put_value<uint16_t>(FRAMEPROPS_FORMAT_VERSION);
    out.put_value<uint16_t>((uint16_t)FRAMEPROPS_HEADER_SIZE);
    out.put_value<uint32_t>(0); // total_size
    out.put_value<uint32_t>(0); // key_count
    out.put_value<uint32_t>(0); // skipped_count
    out.put_value<uint32_t>(0); // reserved
    assert(out.pos() == FRAMEPROPS_HEADER_SIZE);

    uint32_t key_count = 0;
    uint32_t skipped_count = 0;

    for (int k = 0; k < num_keys; k++)
    {
        const char* key = env->propGetKey(map, k);
        const char type = env->propGetType(map, key);
        if (type != PROPTYPE_INT && type != PROPTYPE_FLOAT && type != PROPTYPE_DATA)
        {
            skipped_count++;
            continue;
        }
        const size_t key_len = strlen(key);
        if (key_len > 0xFFFF)
        {
            skipped_count++;
            continue;
        }
        const int n = env->propNumElements(map, key);
        const uint32_t count = n > 0 ? (uint32_t)n : 0;

        out.put_value<uint16_t>((uint16_t)key_len);
        out.put_value<uint8_t>((uint8_t)type);
        out.put_value<uint8_t>(0);
        out.put_value<uint32_t>(count);
        out.put(key, key_len);
        out.align8();

        int error = 0;
        switch (type)
        {
        case PROPTYPE_INT:
            if (count > 0)
            {
                const int64_t* values = env->propGetIntArray(map, key, &error);
                if (error || !values)
                {
                    throw std::runtime_error(std::string("Unable to read frame property ") + key);
                }
                out.put(values, sizeof(int64_t) * count);
            }
            break;
        case PROPTYPE_FLOAT:
            if (count > 0)
            {
                const double* values = env->propGetFloatArray(map, key, &error);
                if (error || !values)
                {
                    throw std::runtime_error(std::string("Unable to read frame property ") + key);
                }
                out.put(values, sizeof(double) * count);
            }
            break;
        case PROPTYPE_DATA:
            for (uint32_t i = 0; i < count; i++)
            {
                const char* data = env->propGetData(map, key, i, &error);
                const int size = error ? 0 : env->propGetDataSize(map, key, i, &error);
                if (error || size < 0)
                {
                    throw std::runtime_error(std::string("Unable to read frame property ") + key);
                }
                int hint = PROPDATATYPEHINT_UNKNOWN;
                if (caps.has_type_hint)
                {
                    hint = env->propGetDataTypeHint(map, key, i, &error);
                    hint = error ? PROPDATATYPEHINT_UNKNOWN : valid_type_hint(hint);
                }
                out.put_value<uint32_t>((uint32_t)size);
                out.put_value<int32_t>(hint);
                out.put(data, (size_t)size);
                out.align8();
            }
            break;
        }
        key_count++;
    }

    if (key_count == 0)
    {
        // only non-transferable properties, nothing to pass
        return 0;
    }
    const size_t total_size = out.pos();
    check_int_size(total_size, "total size");
    if (dst)
    {
        uint32_t v = (uint32_t)total_size;
        memcpy(dst + 8, &v, 4);
        memcpy(dst + 12, &key_count, 4);
        memcpy(dst + 16, &skipped_count, 4);
    }
    return total_size;
}

void deserialize_frame_props(IScriptEnvironment* env, const FramePropsCaps& caps, PVideoFrame& frame, const uint8_t* src, size_t src_size)
{
    if (!caps.has_props || src_size == 0)
    {
        return;
    }
    assert(((uintptr_t)src & 7) == 0);

    Source in(src, src_size);
    if (in.get_value<uint32_t>() != FRAMEPROPS_MAGIC)
    {
        throw std::runtime_error("Malformed frame property block (bad signature).");
    }
    const uint16_t version = in.get_value<uint16_t>();
    const uint16_t header_size = in.get_value<uint16_t>();
    const uint32_t total_size = in.get_value<uint32_t>();
    const uint32_t key_count = in.get_value<uint32_t>();
    if (version != FRAMEPROPS_FORMAT_VERSION || header_size != FRAMEPROPS_HEADER_SIZE)
    {
        throw std::runtime_error("Unsupported frame property block version.");
    }
    if (total_size > src_size || total_size < FRAMEPROPS_HEADER_SIZE)
    {
        throw std::runtime_error("Malformed frame property block (bad size).");
    }
    // restrict reading to the declared block
    Source body(src, total_size);
    body.take(FRAMEPROPS_HEADER_SIZE);

    AVSMap* map = env->getFramePropsRW(frame);
    std::string key;

    for (uint32_t k = 0; k < key_count; k++)
    {
        const uint16_t key_len = body.get_value<uint16_t>();
        const char type = (char)body.get_value<uint8_t>();
        body.get_value<uint8_t>();
        const uint32_t count = body.get_value<uint32_t>();
        key.assign((const char*)body.take(key_len), key_len);
        body.align8();
        if (!is_valid_key(key))
        {
            throw std::runtime_error("Malformed frame property block (invalid key).");
        }

        int failed = 0;
        switch (type)
        {
        case PROPTYPE_INT:
        {
            check_int_size(count, key.c_str());
            const int64_t* values = (const int64_t*)body.take_array(count, sizeof(int64_t));
            failed = env->propSetIntArray(map, key.c_str(), values, (int)count);
            break;
        }
        case PROPTYPE_FLOAT:
        {
            check_int_size(count, key.c_str());
            const double* values = (const double*)body.take_array(count, sizeof(double));
            failed = env->propSetFloatArray(map, key.c_str(), values, (int)count);
            break;
        }
        case PROPTYPE_DATA:
            env->propDeleteKey(map, key.c_str());
            for (uint32_t i = 0; i < count && !failed; i++)
            {
                const uint32_t size = body.get_value<uint32_t>();
                const int32_t hint = valid_type_hint(body.get_value<int32_t>());
                check_int_size(size, key.c_str());
                const char* data = (const char*)body.take(size);
                body.align8();
                failed = caps.has_type_hint
                    ? env->propSetDataH(map, key.c_str(), data, (int)size, hint, PROPAPPENDMODE_APPEND)
                    : env->propSetData(map, key.c_str(), data, (int)size, PROPAPPENDMODE_APPEND);
            }
            break;
        default:
            throw std::runtime_error("Malformed frame property block (unknown type).");
        }
        if (failed)
        {
            throw std::runtime_error("Unable to set frame property " + key);
        }
    }
}
