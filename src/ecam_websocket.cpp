// SPDX-License-Identifier: GPL-3.0-or-later
// PS5-ECAM: the remote touch screen's WebSockets (websocket.h), from dolphin78's prototype (#38).
// Porpoise: PS5-ECAM's port/stream/websocket.cpp at 269e273, its handshake's SHA-1 from
// ecam_sha1.h instead of OpenSSL (which Porpoise does not link); nothing else changed.
#include "ecam_websocket.h"
#include "ecam_sha1.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <fcntl.h>
#include <cerrno>
#include <cctype>
#include <algorithm>

namespace ps5stream
{
void ConfigureSocket(int fd)
{
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	int yes = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
	setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
}

bool SendBytes(int fd, const void* bytes, size_t size, const std::function<bool()>& alive)
{
	const auto* p = static_cast<const uint8_t*>(bytes);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
	while (size && alive() && std::chrono::steady_clock::now() < deadline)
	{
		int flags = 0;
#ifdef MSG_NOSIGNAL
		flags = MSG_NOSIGNAL;
#endif
		const auto n = send(fd, p, size, flags);
		if (n > 0) { p += n; size -= n; continue; }
		if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
			return false;
		pollfd wait{fd, POLLOUT, 0};
		poll(&wait, 1, 20);
	}
	return size == 0;
}

namespace
{
std::string Header(const std::string& request, const std::string& name)
{
	size_t start = request.find("\r\n");
	while (start != std::string::npos)
	{
		start += 2;
		const auto end = request.find("\r\n", start);
		if (end == std::string::npos || end == start) break;
		const auto colon = request.find(':', start);
		if (colon < end)
		{
			std::string field = request.substr(start, colon - start);
			for (auto& c : field) c = std::tolower(static_cast<unsigned char>(c));
			if (field == name)
			{
				auto value = request.substr(colon + 1, end - colon - 1);
				const auto first = value.find_first_not_of(" \t"), last = value.find_last_not_of(" \t");
				return first == std::string::npos ? "" : value.substr(first, last - first + 1);
			}
		}
		start = end;
	}
	return {};
}
}

bool WebSocket::Upgrade(const std::string& request)
{
	std::string upgrade = Header(request, "upgrade"), connection = Header(request, "connection");
	for (auto& c : upgrade) c = std::tolower(static_cast<unsigned char>(c));
	for (auto& c : connection) c = std::tolower(static_cast<unsigned char>(c));
	const auto key = Header(request, "sec-websocket-key");
	// Sec-WebSocket-Key is protocol framing, not an authentication requirement.
	if (upgrade != "websocket" || connection.find("upgrade") == std::string::npos ||
		Header(request, "sec-websocket-version") != "13" || key.size() != 24)
		return false;
	const std::string challenge = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	unsigned char digest[20];
	Sha1(challenge.data(), challenge.size(), digest);
	unsigned char encoded[64];
	const int length = Base64(digest, sizeof digest, encoded);
	const std::string reply = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
		"Connection: Upgrade\r\nSec-WebSocket-Accept: " + std::string(reinterpret_cast<char*>(encoded), length) + "\r\n\r\n";
	const auto end = request.find("\r\n\r\n");
	if (end != std::string::npos)
		input.assign(request.begin() + end + 4, request.end());
	return SendBytes(fd, reply.data(), reply.size(), alive);
}

bool WebSocket::Send(uint8_t opcode, const void* bytes, size_t size)
{
	uint8_t header[10]{static_cast<uint8_t>(0x80 | opcode), 0};
	size_t headerSize = 2;
	if (size < 126) header[1] = static_cast<uint8_t>(size);
	else if (size <= 65535)
	{
		header[1] = 126; header[2] = size >> 8; header[3] = size; headerSize = 4;
	}
	else
	{
		header[1] = 127;
		for (int i = 0; i < 8; ++i) header[2 + i] = static_cast<uint64_t>(size) >> (56 - i * 8);
		headerSize = 10;
	}
	return SendBytes(fd, header, headerSize, alive) && SendBytes(fd, bytes, size, alive);
}

WebSocket::Result WebSocket::Receive(std::string& message, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	for (;;)
	{
		if (input.size() >= 2)
		{
			const uint8_t opcode = input[0] & 15;
			const bool fin = input[0] & 128;
			if ((input[0] & 112) || !(input[1] & 128) ||
				(opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10))
				return Result::Error;
			uint64_t length = input[1] & 127;
			size_t offset = 2;
			if (length == 126)
			{
				if (input.size() < 4) goto read;
				length = (input[2] << 8) | input[3]; offset = 4;
				if (length < 126) return Result::Error;
			}
			else if (length == 127) return Result::Error; // Commands never require 64-bit lengths.
			if (length > 512 || (opcode >= 8 && (!fin || length > 125)) || (opcode == 8 && length == 1)) return Result::Error;
			if (input.size() >= offset + 4 + length)
			{
				message.resize(length);
				for (size_t i = 0; i < length; ++i) message[i] = input[offset + 4 + i] ^ input[offset + i % 4];
				input.erase(input.begin(), input.begin() + offset + 4 + length);
				if (opcode == 8) { Send(8, message.data(), message.size()); return Result::Closed; }
				if (opcode == 9) { if (!Send(10, message.data(), message.size())) return Result::Error; continue; }
				if (opcode == 10) continue;
				if (opcode == 0)
				{
					if (!fragmentOpcode || fragmented.size() + message.size() > 512) return Result::Error;
					fragmented.insert(fragmented.end(), message.begin(), message.end());
					if (!fin) continue;
					message.assign(fragmented.begin(), fragmented.end()); fragmented.clear();
					const auto result = fragmentOpcode == 1 ? Result::Text : Result::Binary;
					fragmentOpcode = 0; return result;
				}
				if (fragmentOpcode) return Result::Error;
				if (!fin) { fragmentOpcode = opcode; fragmented.assign(message.begin(), message.end()); continue; }
				return opcode == 1 ? Result::Text : Result::Binary;
			}
		}
	read:
		if (!alive()) return Result::Closed;
		if (std::chrono::steady_clock::now() >= deadline) return Result::Idle;
		pollfd wait{fd, POLLIN, 0};
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
		if (poll(&wait, 1, std::max(1, std::min(20, int(remaining)))) <= 0) continue;
		uint8_t bytes[1024];
		const auto n = recv(fd, bytes, sizeof(bytes), 0);
		if (n == 0) return Result::Closed;
		if (n < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
			return Result::Error;
		}
		if (input.size() + n > 2048) return Result::Error;
		input.insert(input.end(), bytes, bytes + n);
	}
}
}
