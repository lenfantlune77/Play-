#pragma once

#include <array>
#include <vector>

#include "Stream.h"

class CJsDiscImageDeviceStream : public Framework::CStream
{
public:
	virtual ~CJsDiscImageDeviceStream() = default;

	void Seek(int64, Framework::STREAM_SEEK_DIRECTION) override;
	uint64 Tell() override;
	uint64 Read(void*, uint64) override;
	uint64 Write(const void*, uint64) override;
	bool IsEOF() override;
	void Flush() override;

private:
	//The filesystem reads the disc 2KB at a time, and every read costs a round trip to the browser's
	//main thread while this thread spins. Small chunks are kept instead: a game alternates between
	//distant places, streaming audio here and reading data there, so a single chunk would be missed
	//on every read and would copy far more than it serves.
	enum
	{
		CHUNK_SIZE = 0x20000,
		CHUNK_COUNT = 8,
	};

	struct CHUNK
	{
		std::vector<uint8> data;
		uint64 start = 0;
		uint64 size = 0;
		uint64 used = 0;
	};

	void ReadFromJs(void* buffer, uint64 position, uint64 size);
	CHUNK& ChunkFor(uint64 position);

	uint64 m_position = 0;
	uint64 m_clock = 0;
	std::array<CHUNK, CHUNK_COUNT> m_chunks;
};
