// SPDX-License-Identifier: GPL-3.0-or-later
// Porpoise: SHA-1 and Base64 for the WebSocket handshake (ecam_websocket.cpp), in place of OpenSSL.
// Only the handshake's digest: not for anything that needs to be secure.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ps5stream
{
inline void Sha1(const void* data, size_t size, unsigned char out[20])
{
	uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
	auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
	auto block = [&](const unsigned char* b) {
		uint32_t w[80];
		for (int i = 0; i < 16; ++i)
			w[i] = uint32_t(b[i * 4]) << 24 | uint32_t(b[i * 4 + 1]) << 16 | uint32_t(b[i * 4 + 2]) << 8 | b[i * 4 + 3];
		for (int i = 16; i < 80; ++i)
			w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; ++i)
		{
			uint32_t f, k;
			if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
			else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
			else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
			else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
			const uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(bb, 30); bb = a; a = t;
		}
		h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
	};
	const auto* p = static_cast<const unsigned char*>(data);
	size_t left = size;
	for (; left >= 64; left -= 64, p += 64)
		block(p);
	unsigned char tail[128] = {};
	std::memcpy(tail, p, left);
	tail[left] = 0x80;
	const size_t tail_size = left < 56 ? 64 : 128;
	const uint64_t bits = uint64_t(size) * 8;
	for (int i = 0; i < 8; ++i)
		tail[tail_size - 1 - i] = static_cast<unsigned char>(bits >> (8 * i));
	block(tail);
	if (tail_size == 128)
		block(tail + 64);
	for (int i = 0; i < 5; ++i)
		for (int j = 0; j < 4; ++j)
			out[i * 4 + j] = static_cast<unsigned char>(h[i] >> (24 - 8 * j));
}

// Returns the length written (no terminator).
inline int Base64(const unsigned char* in, size_t size, unsigned char* out)
{
	static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	int o = 0;
	for (size_t i = 0; i < size; i += 3)
	{
		const uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < size ? uint32_t(in[i + 1]) << 8 : 0) |
						   (i + 2 < size ? in[i + 2] : 0);
		out[o++] = kAlphabet[(v >> 18) & 63];
		out[o++] = kAlphabet[(v >> 12) & 63];
		out[o++] = i + 1 < size ? kAlphabet[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < size ? kAlphabet[v & 63] : '=';
	}
	return o;
}
}
