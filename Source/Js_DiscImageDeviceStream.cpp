#include "Js_DiscImageDeviceStream.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <cassert>
#include <emscripten.h>
#include <unistd.h>

void CJsDiscImageDeviceStream::Seek(int64 position, Framework::STREAM_SEEK_DIRECTION whence)
{
	switch(whence)
	{
	case Framework::STREAM_SEEK_SET:
		m_position = position;
		break;
	case Framework::STREAM_SEEK_CUR:
		m_position += position;
		break;
	case Framework::STREAM_SEEK_END:
	{
		uint32 positionLo = MAIN_THREAD_EM_ASM_INT({return Module.discImageDevice.getFileSize()});
		uint32 positionHi = MAIN_THREAD_EM_ASM_INT({return Module.discImageDevice.getFileSize() / 4294967296});
		m_position = static_cast<uint64>(positionLo) | (static_cast<uint64>(positionHi) << 32);
	}
	break;
	}
}

uint64 CJsDiscImageDeviceStream::Tell()
{
	return m_position;
}

void CJsDiscImageDeviceStream::ReadFromJs(void* buffer, uint64 position, uint64 size)
{
	uint32 positionLow = static_cast<uint32>(position);
	uint32 positionHigh = static_cast<uint32>(position >> 32);

	MAIN_THREAD_EM_ASM({
		let posLow = $1 >>> 0;
		let posHigh = $2 >>> 0;
		let position = posLow + (posHigh * 4294967296);
		Module.discImageDevice.read($0, position, $3);
	},
	                   buffer, positionLow, positionHigh, static_cast<uint32>(size));
	while(!MAIN_THREAD_EM_ASM_INT({return Module.discImageDevice.isDone()}))
	{
		usleep(100);
	}
}

CJsDiscImageDeviceStream::CHUNK& CJsDiscImageDeviceStream::ChunkFor(uint64 position)
{
	uint64 start = (position / CHUNK_SIZE) * CHUNK_SIZE;
	m_clock++;

	CHUNK* oldest = &m_chunks[0];
	for(auto& chunk : m_chunks)
	{
		if((chunk.size != 0) && (chunk.start == start))
		{
			chunk.used = m_clock;
			return chunk;
		}
		if(chunk.used < oldest->used) oldest = &chunk;
	}

	if(oldest->data.size() != CHUNK_SIZE) oldest->data.resize(CHUNK_SIZE);
	oldest->start = start;
	oldest->size = CHUNK_SIZE;
	oldest->used = m_clock;
	ReadFromJs(oldest->data.data(), start, CHUNK_SIZE);
	return *oldest;
}

uint64 CJsDiscImageDeviceStream::Read(void* buffer, uint64 size)
{
	if(size == 0) return 0;

	assert(size <= std::numeric_limits<uint32>::max());

	if(size >= CHUNK_SIZE)
	{
		//Too large to be worth caching: straight through.
		ReadFromJs(buffer, m_position, size);
		m_position += size;
		return size;
	}

	auto destination = reinterpret_cast<uint8*>(buffer);
	uint64 done = 0;
	while(done < size)
	{
		uint64 position = m_position + done;
		auto& chunk = ChunkFor(position);
		uint64 inner = position - chunk.start;
		uint64 count = std::min<uint64>(size - done, chunk.size - inner);
		memcpy(destination + done, chunk.data.data() + inner, count);
		done += count;
	}
	m_position += size;
	return size;
}

uint64 CJsDiscImageDeviceStream::Write(const void*, uint64)
{
	throw std::runtime_error("Not supported.");
}

bool CJsDiscImageDeviceStream::IsEOF()
{
	throw std::runtime_error("Not supported.");
}

void CJsDiscImageDeviceStream::Flush()
{
}
