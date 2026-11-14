#ifndef MP3_DECODER_H
#define MP3_DECODER_H

#include <stddef.h>

#include "audio.h"
#include "db.h"

namespace fallout {

bool mp3DecoderIsFilePath(const char* filePath);
bool mp3DecoderDecode(File* stream, AudioFileInfo* info, unsigned char** dataPtr, int* sizePtr);

// Decodes a whole MP3 held in memory to interleaved 16-bit PCM. The output
// buffer is allocated with internal_malloc_safe; release it with
// internal_free_safe.
bool mp3DecoderDecodeMemory(const unsigned char* mp3Data, size_t mp3Size, AudioFileInfo* info, unsigned char** dataPtr, int* sizePtr);

} // namespace fallout

#endif /* MP3_DECODER_H */
