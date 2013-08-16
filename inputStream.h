/*
 * inputStream.h
 *
 *  Created on: Aug 10, 2013
 *      Author: gokhan
 */

#ifndef INPUTSTREAM_H_
#define INPUTSTREAM_H_

#include <stdio.h>
#include "orc.pb-c.h"

#define DEFAULT_BUFFER_SIZE 262144
#define DEFAULT_TEMP_BUFFER_SIZE 30

typedef struct
{
	CompressionKind compressionKind;
	long compressionBlockSize;
} CompressionParameters;

typedef struct
{
	/* Input stream for reading from the file */
	FILE* file;

	/* offset of the start of the buffer in the file */
	long offset;
	/* end of the file stream in the file */
	long limit;

	/* current position in the buffer */
	int position;
	/* current length of the buffer */
	int length;

	/* allocated buffer size */
	int bufferSize;
	/* buffer to store the file data */
	char* buffer;
} FileBuffer;

typedef struct
{
	/* stream for decompression */
	FileBuffer* fileBuffer;

	/* block size used for compression */
	int bufferSize;
	CompressionKind compressionKind;

	/* current length in the uncompressed buffer */
	int position;
	/* current length of the uncompressed buffer */
	int length;
	/* buffer to store uncompressed data */
	char* data;
	/**
	 * When stream is very small, it is not compressed. This byte is for providing that information.
	 * 1 if uncompressed stream is the same as compressed one,
	 * 0 if stream is a "really" compressed one.
	 */
	char isNotCompressed;

	/* this is for reading bytes from cross boundries */
	char* tempBuffer;
	int tempBufferSize;

	/*
	 * Memory is allocated only once for this structure.
	 * This is used for storing the data pointer when isNotCompressed == 1.
	 */
	char* allocatedMemory;
} FileStream;

FileStream* FileStreamInit(char* filePath, long offset, long limit, int bufferSize,
		CompressionKind kind);
int FileStreamFree(FileStream*);
char* FileStreamRead(FileStream*, int *length);
int FileStreamEOF(FileStream* fileStream);
int FileStreamReadRemaining(FileStream*, char** data, int* dataLength);
int FileStreamReadByte(FileStream* stream, char* value);

#endif /* INPUTSTREAM_H_ */
