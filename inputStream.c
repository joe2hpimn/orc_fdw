/*
 * inputStream.c
 *
 *  Created on: Aug 10, 2013
 *      Author: gokhan
 */
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "inputStream.h"
#include "orcUtil.h"
#include "snappy.h"


/*
 * Resets a file buffer with the new data positions
 */
static void
FileBufferReset(FileBuffer *fileBuffer, long offset, long limit, int bufferSize)
{
	fileBuffer->offset = offset;

	if(fileBuffer->bufferSize < bufferSize)
	{
		fileBuffer->bufferSize = bufferSize;
		fileBuffer->buffer = reAllocateMemory(fileBuffer->buffer, bufferSize);
	}

	fileBuffer->limit = limit;

	fileBuffer->position = 0;
	fileBuffer->length = 0;
}


/**
 * Initialize a FileBuffer.
 *
 * @param filePath file to read
 * @param offset starting position in the file
 * @param limit end of the stream in the file
 * @param bufferSize the maximum size that can be read at a time.
 *
 * @return NULL for failure, non-NULL for success
 */
static FileBuffer *
FileBufferInit(FILE *file, long offset, long limit, int bufferSize)
{
	FileBuffer *fileBuffer = alloc(sizeof(FileBuffer));

	fileBuffer->file = file;
	if (fileBuffer->file == NULL)
	{
		freeMemory(fileBuffer);
		return NULL;
	}

	fileBuffer->offset = offset;
	fileBuffer->bufferSize = bufferSize;
	fileBuffer->buffer = alloc(bufferSize);
	fileBuffer->limit = limit;

	if (fileBuffer->buffer == NULL)
	{
		freeMemory(fileBuffer);
		return NULL;
	}

	fileBuffer->position = 0;
	fileBuffer->length = 0;

	return fileBuffer;
}


/**
 * Frees up a file buffer
 *
 * @param fileBuffer file stream to free
 *
 * @return 0 for success, -1 for failure
 */
static int
FileBufferFree(FileBuffer *fileBuffer)
{
	if (fileBuffer == NULL)
	{
		return 0;
	}

	if (fileBuffer->buffer)
	{
		freeMemory(fileBuffer->buffer);
	}

	freeMemory(fileBuffer);

	return 0;
}


/**
 * Static function to fill the buffer. Shifts the read bytes out of the buffer and
 * puts that many (if there are) bytes at the end of the stream
 *
 * @param fileBuffer file buffer to fill
 *
 * @return no of bytes read from the file when the buffer is filled. -1 for failure
 */
static int
FileBufferFill(FileBuffer *fileBuffer)
{
	int byteCount = 0;
	int result = 0;

	if (fileBuffer == NULL)
	{
		return -1;
	}

	if (fileBuffer->offset >= fileBuffer->limit)
	{
		/* already reached end of file stream */
		return 0;
	}

	/* discard the already read values */
	if (fileBuffer->position > 0)
	{

		memmove(fileBuffer->buffer, fileBuffer->buffer + fileBuffer->position,
				fileBuffer->length - fileBuffer->position);
		fileBuffer->length -= fileBuffer->position;
		fileBuffer->position = 0;
	}

	byteCount = Min(fileBuffer->limit - fileBuffer->offset, fileBuffer->bufferSize - fileBuffer->length);

	if (byteCount < 0)
	{
		return -1;
	}
	else if (byteCount == 0)
	{
		/* buffer is already filled */
		return 0;
	}

	/* seek to the current position since the same file is used by many streams */
	if (ftell(fileBuffer->file) != fileBuffer->offset)
	{
		result = fseek(fileBuffer->file, fileBuffer->offset, SEEK_SET);
		if (result)
		{
			LogError("Error occurred while seeking in the file");
		}
	}

	result = fread(fileBuffer->buffer + fileBuffer->length, 1, byteCount, fileBuffer->file);

	if (result != byteCount)
	{
		LogError("Error occurred while reading file\n");
		return -1;
	}

	fileBuffer->length += byteCount;
	fileBuffer->offset += byteCount;

	return byteCount;
}


/**
 * Reads bytes from the file of specified length and returns the start of the data;
 * WARNING! This function returns the pointer from the internal buffer,
 * so copy this to another memory location when necessary!
 *
 * @param fileStream file stream to read
 * @param length when the function returns it writes the actual bytes read from the stream
 *
 * @return pointer to the data in the stream buffer
 */
static char *
FileBufferRead(FileBuffer *fileBuffer, int *length)
{
	char *data = NULL;
	int result = 0;

	if (fileBuffer == NULL)
	{
		return NULL;
	}

	if (*length > fileBuffer->length - fileBuffer->position)
	{
		result = FileBufferFill(fileBuffer);
		if (result <= 0)
		{
			return NULL;
		}

		if (*length > fileBuffer->length - fileBuffer->position)
		{
			/* cannot read that many bytes from the file */
			*length = fileBuffer->length - fileBuffer->position;
		}
	}

	data = fileBuffer->buffer + fileBuffer->position;
	fileBuffer->position += *length;

	return data;
}


/**
 * Reads one byte from the file
 *
 * @param fileStream file stream to read
 * @param value used to store the byte
 *
 * @return 0 for success, -1 for failure
 */
static int
FileBufferReadByte(FileBuffer *fileBuffer, char *value)
{
	int result = 0;

	if (fileBuffer->position >= fileBuffer->length)
	{
		result = FileBufferFill(fileBuffer);
		if (result < 0)
		{
			return -1;
		}

		if (fileBuffer->position >= fileBuffer->length)
		{
			return -1;
		}
	}

	*value = fileBuffer->buffer[fileBuffer->position];
	fileBuffer->position++;

	return 0;
}


/**
 * Bytes left in the stream. Remaining length is calculated by
 * # unread bytes in the file + # unread bytes in the internal buffer
 *
 * @param fileStream stream to query
 *
 * @return no of bytes left unread in the stream
 */
static long
FileBufferBytesLeft(FileBuffer *fileBuffer)
{
	return (fileBuffer->limit - fileBuffer->offset) + (fileBuffer->length - fileBuffer->position);
}


/**
 * Read all the remaining data in the stream.
 *
 * @param fileStream stream to read
 * @param data used to return the data buffer
 * @param dataLength length of the data read
 *
 * @return 0 for success, -1 for failure
 */
static int
FileBufferReadRemaining(FileBuffer *fileBuffer, char **data, int *dataLength)
{
	int remainingLength = FileBufferBytesLeft(fileBuffer);

	if (remainingLength == 0)
	{
		return 0;
	}

	if (remainingLength <= fileBuffer->bufferSize)
	{
		/* try to fill the buffer if necessary */
		FileBufferFill(fileBuffer);
		*data = fileBuffer->buffer + fileBuffer->position;
		*dataLength = fileBuffer->length - fileBuffer->position;
	}
	else
	{
		/* else allocate a new, bigger block */
		fileBuffer->buffer = reAllocateMemory(fileBuffer->buffer, remainingLength);
		fileBuffer->bufferSize = remainingLength;

		/* fill the new buffer */
		if (FileBufferFill(fileBuffer) != remainingLength)
		{
			return -1;
		}

		*data = fileBuffer->buffer + fileBuffer->position;
		*dataLength = fileBuffer->length - fileBuffer->position;
		fileBuffer->position = fileBuffer->length;
	}
	return 0;
}


/* skip to the absolute offset given as the parameter */
static void
FileBufferSkip(FileBuffer *fileBuffer, long offset)
{
	if ((fileBuffer->offset - fileBuffer->length) <= offset && fileBuffer->offset > offset)
	{
		/* if offset is reachable just change the position */
		fileBuffer->position = fileBuffer->length - (fileBuffer->offset - offset);
	}
	else
	{
		/* else we have to read block which starts at offset*/
		fileBuffer->offset = offset;
		fileBuffer->position = 0;
		fileBuffer->length = 0;
		FileBufferFill(fileBuffer);
	}
}

/*
 * Resets the file stream with the new parameters
 */
void
FileStreamReset(FileStream *stream, long offset, long limit, int bufferSize, CompressionKind kind)
{
	/* when uncompressed files are used, buffer size is set to ORC's default size */
	if (kind == COMPRESSION_KIND__NONE)
	{
		bufferSize = DEFAULT_BUFFER_SIZE;
	}

	if (stream->bufferSize < bufferSize)
	{
		stream->bufferSize = bufferSize;
		stream->allocatedMemory = reAllocateMemory(stream->allocatedMemory, stream->bufferSize);
		stream->data = stream->allocatedMemory;
	}

	FileBufferReset(stream->fileBuffer, offset, limit, stream->bufferSize);

	stream->compressionKind = kind;

	stream->position = 0;
	stream->length = 0;
	stream->isNotCompressed = 0;

	stream->startOffset = offset;
	stream->currentCompressedBlockOffset = offset;
}

/**
 * Initialize a CompressedFileStream.
 *
 * @param filePath file to read
 * @param offset starting position in the file
 * @param limit end of the stream in the file
 * @param bufferSize the maximum size that can be read at a time
 * @param kind compression format
 *
 * @return NULL for failure, non-NULL for success
 */
FileStream *
FileStreamInit(FILE *file, long offset, long limit, int bufferSize,
		CompressionKind kind)
{
	FileStream *stream = alloc(sizeof(FileStream));

	if (kind == COMPRESSION_KIND__NONE)
	{
		stream->bufferSize = DEFAULT_BUFFER_SIZE;
	}
	else
	{
		stream->bufferSize = bufferSize;
	}

	stream->fileBuffer = FileBufferInit(file, offset, limit, stream->bufferSize);

	if (stream->fileBuffer == NULL)
	{
		freeMemory(stream);
		return NULL;
	}

	stream->compressionKind = kind;

	stream->position = 0;
	stream->length = 0;
	stream->data = alloc(stream->bufferSize);
	stream->allocatedMemory = stream->data;
	stream->isNotCompressed = 0;

	stream->startOffset = offset;
	stream->currentCompressedBlockOffset = offset;

	/**
	 *  Just allocate some bytes for this buffer since we may not use it.
	 *  This is done in order to overcome PostgreSQL memory context problem
	 */
	stream->tempBuffer = alloc(DEFAULT_TEMP_BUFFER_SIZE);
	stream->tempBufferSize = DEFAULT_TEMP_BUFFER_SIZE;

	return stream;
}


/**
 * Frees up a compressed file stream
 *
 * @param stream stream to free
 *
 * @return 0 for success, -1 for failure
 */
int
FileStreamFree(FileStream *stream)
{
	if (stream == NULL)
	{
		return 0;
	}

	freeMemory(stream->allocatedMemory);

	if (stream->tempBuffer)
	{
		freeMemory(stream->tempBuffer);
	}

	if (FileBufferFree(stream->fileBuffer))
	{
		return -1;
	}

	freeMemory(stream);

	return 0;
}


/**
 * Read the header of the compressed block and do the decompression
 *
 * @param stream stream of the block to decompress
 *
 * @return 0 for success, -1 for failure
 */
static int
ReadNextCompressedBlock(FileStream *stream)
{
	char *header = NULL;
	int headerLength = COMPRESSED_HEADER_SIZE;
	int bufferSize = stream->bufferSize;
	char isNotCompressed = 0;
	int chunkLength = 0;
	int result = 0;

	/*
	 * Since fileBuffer->offset gives the offset of the next byte to read,
	 * now it points to the next compressed block.
	 */
	stream->currentCompressedBlockOffset = stream->fileBuffer->offset;

	header = FileBufferRead(stream->fileBuffer, &headerLength);

	if (header == NULL || headerLength != COMPRESSED_HEADER_SIZE)
	{
		/* couldn't read compressed header */
		return -1;
	}

	/* first 3 bytes are the chunk contains the chunk length, last bit is for "original" */
	chunkLength = ((0xff & header[2]) << 15) | ((0xff & header[1]) << 7) |
			((0xff & header[0]) >> 1);

	if (chunkLength > bufferSize)
	{
		LogError3("Buffer size too small. size = %d needed = %d\n", bufferSize, chunkLength);
		return -1;
	}

	/*
	 * Very small streams are not compressed while writing to the file.
	 * If not compressed, use the FileStreamBuffer's internal buffer
	 */
	isNotCompressed = header[0] & 0x01;

	if (isNotCompressed)
	{
		stream->isNotCompressed = 1;

		/* result is used for temporary storage */
		result = chunkLength;
		stream->data = FileBufferRead(stream->fileBuffer, &chunkLength);

		if (result != chunkLength)
		{
			LogError("chunk of given length couldn't read from the file\n");
			return -1;
		}

		stream->position = 0;
		stream->length = chunkLength;
	}
	else
	{
		char *compressedBuffer = NULL;
		stream->isNotCompressed = 0;

		/**
		 * Get back memory pointer into data pointer since previous stream may not be compressed
		 * and we were file buffer data directly.
		 */
		stream->data = stream->allocatedMemory;

		stream->position = 0;
		stream->length = bufferSize;
		result = chunkLength;

		/* read the compressed block from the file */
		compressedBuffer = FileBufferRead(stream->fileBuffer, &chunkLength);

		if (compressedBuffer == NULL || result != chunkLength)
		{
			LogError("Chunk of given length couldn't read from the file\n");
			return -1;
		}

		/* decompress the block */
		switch (stream->compressionKind)
		{
			case COMPRESSION_KIND__ZLIB:
			{
				result = InflateZLIB((uint8_t *) compressedBuffer, chunkLength, (uint8_t *) stream->data,
						&stream->length);

				if (result != Z_OK)
				{
					LogError("Error occurred while decompressing with zlib inflator\n");
					return -1;
				}

				break;
			}
			case COMPRESSION_KIND__SNAPPY:
			{
				size_t snappyUncompressedSize = 0;
				result = snappy_uncompressed_length((const char *) compressedBuffer, (size_t) chunkLength,
						&snappyUncompressedSize);

				if (result != 1)
				{
					LogError("Error occurred while calculating uncompressed size of snappy block.\n");
					return 1;
				}

				stream->length = (int) snappyUncompressedSize;

				if (stream->length > stream->bufferSize)
				{
					LogError3("Uncompressed stream size (%d) exceeds buffer size (%d\n", stream->length,
							stream->bufferSize);
					return -1;
				}

				result = snappy_uncompress((const char*) compressedBuffer, (size_t) chunkLength,
						(char *) stream->data);

				if (result)
				{
					LogError2("Error occurred while uncompressing with snappy. Error code %d\n", result);
					return -1;
				}

				break;
			}
			default:
			{
				/* other compression kinds are not supported */
				LogError2("Compression kind is unsupported. ID: %d", stream->compressionKind);
				return -1;
			}
		}
	}
	return 0;
}


/**
 * Reads bytes from the file of specified length and returns the start of the data;
 * WARNING! This function returns the pointer from the internal buffer,
 * so copy this to another memory location when necessary!
 *
 * @param stream stream to read
 * @param length when the function returns it writes the actual bytes read from the stream
 *
 * @return pointer to the data in the stream buffer
 */
char *
FileStreamRead(FileStream *stream, int *length)
{
	int requestedLength = *length;
	char *data = NULL;
	int result = 0;

	if (stream->compressionKind == COMPRESSION_KIND__NONE)
	{
		/* if there is no compression, read directly from FileBuffer */
		return FileBufferRead(stream->fileBuffer, length);
	}

	/* if the stream is empty or no bytes left in it, read the next compressed block */ 
	if (stream->length == 0 || stream->position == stream->length)
	{
		result = ReadNextCompressedBlock(stream);

		if (result)
		{
			LogError("Error reading compressed stream header\n");
			return NULL;
		}
	}

	if (stream->position + requestedLength <= stream->length)
	{
		/* if requested bytes can be provided from the get the offset */
		data = stream->data + stream->position;
		stream->position += requestedLength;
	}
	else
	{
		/* 
		 * If requested length is greater than the bytes left in the stream,
		 * also read from the next compressed block.
		 */
		int unreadByteCount = 0;

		/* use temp buffer to return the requested bytes */
		if (stream->tempBufferSize < requestedLength)
		{
			stream->tempBuffer = reAllocateMemory(stream->tempBuffer,requestedLength);
			stream->tempBufferSize = requestedLength;
		}

		/* copy unread bytes from the data buffer */
		unreadByteCount = stream->length - stream->position;
		memcpy(stream->tempBuffer, stream->data + stream->position, unreadByteCount);

		result = ReadNextCompressedBlock(stream);

		if (result)
		{
			LogError("Error occurred while initializing the next block\n");
			return NULL;
		}

		if (stream->length < requestedLength - unreadByteCount)
		{
			LogError("Requested data size exceeds compression block size\n");
			return NULL;
		}

		/* copy remaining bytes from the next block */
		memcpy(stream->tempBuffer + unreadByteCount, stream->data, requestedLength - unreadByteCount);
		stream->position += requestedLength - unreadByteCount;

		data = stream->tempBuffer;
	}

	return data;
}


/**
 * Reads one byte from the file of specified
 *
 * @param stream stream to read
 * @param value pointer to the char to store the value in
 *
 * @return 0 for success, -1 for failure
 */
int
FileStreamReadByte(FileStream *stream, char *value)
{
	int result = 0;

	if (stream->compressionKind == COMPRESSION_KIND__NONE)
	{
		/* if there is no compression, read directly from FileBuffer */
		return FileBufferReadByte(stream->fileBuffer, value);
	}

	if (stream->length == 0 || (stream->position == stream->length))
	{
		result = ReadNextCompressedBlock(stream);

		if (result)
		{
			LogError("Error reading compressed stream header\n");
			return -1;
		}
	}

	if (stream->position < stream->length)
	{
		*value = stream->data[stream->position];
		stream->position++;
		return 0;
	}
	else
	{
		return -1;
	}
}


/**
 * Read all the remaining data in the stream.
 *
 * @param stream stream to read
 * @param data used to return the data buffer
 * @param dataLength to write the actual length of the data read
 *
 * @return 0 for success, -1 for failure
 */
int
FileStreamReadRemaining(FileStream *stream, char **data, int *dataLength)
{
	int result = 0;

	if (stream->compressionKind == COMPRESSION_KIND__NONE)
	{
		/* if there is no compression, read directly from FileBuffer */
		return FileBufferReadRemaining(stream->fileBuffer, data, dataLength);
	}

	if (stream->length == 0 || (stream->position == stream->length))
	{
		/* if not any bytes is read from the stream before, init it first */
		result = ReadNextCompressedBlock(stream);
		if (result)
		{
			LogError("Error reading compressed stream header\n");
			return -1;
		}
	}

	if (FileBufferBytesLeft(stream->fileBuffer))
	{
		int tempBufferPosition = 0;

		/* make a larger buffer for reading remaining data */
		stream->tempBufferSize = stream->bufferSize * 2;
		stream->tempBuffer = reAllocateMemory(stream->tempBuffer, stream->tempBufferSize);

		/* put the temporary buffer position beforehand and copy from stream data */
		tempBufferPosition = stream->length - stream->position;
		memcpy(stream->tempBuffer, stream->data + stream->position, tempBufferPosition);

		/* while file has still data, decompress the block and append it to temporary buffer */
		while (FileBufferBytesLeft(stream->fileBuffer))
		{
			result = ReadNextCompressedBlock(stream);
			if (result)
			{
				LogError("Error reading compressed stream header\n");
				return -1;
			}

			if (stream->tempBufferSize - tempBufferPosition < stream->length - stream->position)
			{
				/* if there is not space in the new buffer double its size */
				stream->tempBufferSize *= 2;
				stream->tempBuffer = reAllocateMemory(stream->tempBuffer, stream->tempBufferSize);
			}

			memcpy(stream->tempBuffer + tempBufferPosition, stream->data, stream->length);
			tempBufferPosition += stream->length;
			stream->position = stream->length;
		}

		/* finally return the temporary buffer as the data */
		*data = stream->tempBuffer;
		*dataLength = tempBufferPosition;
	}
	else
	{
		/* if file doesn't have more compressed blocks, return the data in the buffer */
		*data = stream->data + stream->position;
		*dataLength = stream->length - stream->position;
	}

	return 0;
}


/**
 * Checks whether file stream is ended.
 *
 * @return 1 if file is ended, 0 otherwise
 */
int
FileStreamEOF(FileStream *fileStream)
{
	return fileStream->position == fileStream->length && FileBufferBytesLeft(fileStream->fileBuffer) == 0;
}


/*
 * Skip to the given offset in the file stream
 *
 * @param stream file stream to skip
 * @param stack contains the positions in the streams and the runs
 */
void
FileStreamSkip(FileStream *stream, OrcStack *stack)
{
	long *fileOffset = OrcStackPop(stack);

	if (!fileOffset)
	{
		LogError("Not enough position offset to skip");
	}

	if (*fileOffset + stream->startOffset != stream->currentCompressedBlockOffset)
	{
		/* Skip to the (un)compressed block at the file.
		 * File offsets are the offsets starting from the data stream.
		 */
		FileBufferSkip(stream->fileBuffer, *fileOffset + stream->startOffset);
	}

	/* if stream is compressed, skip bytes also in the uncompressed data */
	if (stream->compressionKind != COMPRESSION_KIND__NONE)
	{
		long *blockOffset = OrcStackPop(stack);

		if (!blockOffset)
		{
			LogError("Not enough position offset to skip");
		}

		/*
		 * If the current compressed block offset doesn't match with the given one,
		 * it means that we have to uncompress that block. Else, we just skip to
		 * the given offset in the uncompressed block.
		 */
		if (*fileOffset + stream->startOffset != stream->currentCompressedBlockOffset)
		{
			ReadNextCompressedBlock(stream);
		}

		if (*blockOffset >= stream->length)
		{
			LogError("Not enough bytes to skip in the uncompressed stream");
		}

		/* if compressed, position the next byte to the given offset */
		stream->position = *blockOffset;
	}
}
