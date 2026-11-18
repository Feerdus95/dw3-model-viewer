/* Little-endian reading with bounds checks (a read past the end throws, as Python's IndexError/struct.error did in the
   tools this replaces) and packing with range checks (struct.pack's). Shared by the disc readers of the DW2 and DCB tabs. */
#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

typedef std::vector<uint8_t> Bytes;

struct Span {
    const uint8_t *p = nullptr;
    size_t n = 0;
    Span() {}
    Span(const Bytes &b) : p(b.data()), n(b.size()) {}
    Span(const uint8_t *q, size_t len) : p(q), n(len) {}
    /* d[a:a+len] as Python slices it: clipped to the data */
    Span sub(size_t a, size_t len = (size_t)-1) const {
        if (a > n) a = n;
        return Span(p + a, len > n - a ? n - a : len);
    }
    void need(size_t a, size_t len) const {
        if (a > n || len > n - a) throw std::out_of_range("read past the end");
    }
    uint8_t u8(size_t a) const { return need(a, 1), p[a]; }
    int s8(size_t a) const { return (int8_t)u8(a); }
    int u16(size_t a) const { return need(a, 2), p[a] | p[a + 1] << 8; }
    int s16(size_t a) const { return (int16_t)u16(a); }
    uint32_t u32(size_t a) const { return need(a, 4), p[a] | p[a + 1] << 8 | p[a + 2] << 16 | (uint32_t)p[a + 3] << 24; }
    int32_t s32(size_t a) const { return (int32_t)u32(a); }
};

struct Out {
    Bytes b;
    void u8(int v) { range(v, 0, 255), b.push_back((uint8_t)v); }
    void u16(long long v) { range(v, 0, 65535), b.push_back(v & 255), b.push_back((v >> 8) & 255); }
    void s16(long long v) { range(v, -32768, 32767), u16(v & 0xFFFF); }
    void raw(const uint8_t *p, size_t n) { b.insert(b.end(), p, p + n); }
    void raw(const Bytes &o) { b.insert(b.end(), o.begin(), o.end()); }
    void raw(const char *s) { raw((const uint8_t *)s, strlen(s)); }
    static void range(long long v, long long lo, long long hi) {
        if (v < lo || v > hi) throw std::range_error("value out of range for its field");
    }
};
