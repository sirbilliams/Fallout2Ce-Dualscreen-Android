#include "mp3_decoder.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "memory_manager.h"
#include "platform_compat.h"

#define DR_MP3_NO_STDIO
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

namespace fallout {

bool mp3DecoderIsFilePath(const char* filePath)
{
    const char* dot = strrchr(filePath, '.');
    return dot != nullptr && compat_stricmp(dot + 1, "mp3") == 0;
}

bool mp3DecoderDecodeMemory(const unsigned char* mp3Data, size_t mp3Size, AudioFileInfo* info, unsigned char** dataPtr, int* sizePtr)
{
    if (mp3Data == nullptr || mp3Size == 0) {
        return false;
    }

    drmp3_config config = {};
    drmp3_uint64 frameCount = 0;
    drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(mp3Data, mp3Size, &config, &frameCount, nullptr);
    if (pcm == nullptr) {
        return false;
    }

    bool success = false;
    if (frameCount > 0 && config.channels >= 1 && config.channels <= 2 && config.sampleRate > 0) {
        drmp3_uint64 bytes = frameCount * config.channels * sizeof(short);
        if (bytes <= INT_MAX) {
            if (info != nullptr) {
                info->channels = static_cast<int>(config.channels);
                info->sampleRate = static_cast<int>(config.sampleRate);
                info->bitsPerSample = 16;
            }

            if (dataPtr != nullptr && sizePtr != nullptr) {
                unsigned char* out = reinterpret_cast<unsigned char*>(internal_malloc_safe(static_cast<size_t>(bytes), __FILE__, __LINE__));
                memcpy(out, pcm, static_cast<size_t>(bytes));
                *dataPtr = out;
                *sizePtr = static_cast<int>(bytes);
            }

            success = true;
        }
    }

    drmp3_free(pcm, nullptr);
    return success;
}

bool mp3DecoderDecode(File* stream, AudioFileInfo* info, unsigned char** dataPtr, int* sizePtr)
{
    int fileSize = fileGetSize(stream);
    if (fileSize <= 0) {
        return false;
    }

    unsigned char* compressedData = reinterpret_cast<unsigned char*>(internal_malloc_safe(fileSize, __FILE__, __LINE__));
    bool success = false;
    if (fileRead(compressedData, 1, fileSize, stream) == fileSize) {
        success = mp3DecoderDecodeMemory(compressedData, fileSize, info, dataPtr, sizePtr);
    }

    internal_free_safe(compressedData, __FILE__, __LINE__);
    return success;
}

} // namespace fallout
