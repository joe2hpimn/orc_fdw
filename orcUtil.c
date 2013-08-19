#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "orcUtil.h"
#include "snappy.h"

#define TIMESPEC_BUFFER_LENGTH 30

int TimespecToStr(char* timespecBuffer, struct timespec *ts)
{
	int ret = 0;
	int len = TIMESPEC_BUFFER_LENGTH;
	struct tm t;

	tzset();
	if (localtime_r(&(ts->tv_sec), &t) == NULL)
		return 1;

	ret = strftime(timespecBuffer, len, "%F %T", &t);
	if (ret == 0)
		return 2;
	len -= ret - 1;

	ret = snprintf(&timespecBuffer[strlen(timespecBuffer)], len, ".%09ld", ts->tv_nsec);
	if (ret >= len)
		return 3;

	return 0;
}

int InflateZLIB(uint8_t *input, int inputSize, uint8_t *output, int *outputSize)
{
	int ret = 0;
	z_stream strm;

	/* allocate inflate state */
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.avail_in = 0;
	strm.next_in = Z_NULL;
	ret = inflateInit2(&strm,-15);
	if (ret != Z_OK)
		return ret;

	strm.avail_in = inputSize;
	if (strm.avail_in == 0)
		return Z_DATA_ERROR;
	strm.next_in = input;

	strm.avail_out = *outputSize;
	strm.next_out = output;
	ret = inflate(&strm, Z_NO_FLUSH);
	assert(ret != Z_STREAM_ERROR); /* state not clobbered */
	switch (ret)
	{
	case Z_NEED_DICT:
		ret = Z_DATA_ERROR; /* and fall through */
	case Z_DATA_ERROR:
	case Z_MEM_ERROR:
		(void) inflateEnd(&strm);
		return ret;
	}

	*outputSize = *outputSize - strm.avail_out;

	/* clean up and return */
	(void) inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

void PrintFieldValue(FILE* file, FieldValue* value, FieldType__Kind kind, int length)
{
	char* timespecBuffer = NULL;
	uint8_t *binaryValues = NULL;
	int iterator = 0;

	switch (kind)
	{
	case FIELD_TYPE__KIND__BOOLEAN:
		fprintf(file, "%d", (int) value->value8);
		break;
	case FIELD_TYPE__KIND__BYTE:
		fprintf(file, "%.2X", value->value8);
		break;
	case FIELD_TYPE__KIND__SHORT:
	case FIELD_TYPE__KIND__INT:
	case FIELD_TYPE__KIND__LONG:
		fprintf(file, "%ld", value->value64);
		break;
	case FIELD_TYPE__KIND__FLOAT:
		fprintf(file, "%.2f", value->floatValue);
		break;
	case FIELD_TYPE__KIND__DOUBLE:
		fprintf(file, "%.2lf", value->doubleValue);
		break;
	case FIELD_TYPE__KIND__STRING:
		fprintf(file, "%s", value->binary);
		break;
	case FIELD_TYPE__KIND__TIMESTAMP:
		timespecBuffer = alloc(TIMESPEC_BUFFER_LENGTH);
		TimespecToStr(timespecBuffer, &value->time);
		fprintf(file, "%s", timespecBuffer);
		break;
	case FIELD_TYPE__KIND__BINARY:
		binaryValues = (uint8_t*) value->binary;
		for (iterator = 0; iterator < length; ++iterator)
		{
			fprintf(file, "%.2X", binaryValues[iterator]);
		}
		break;
	default:
		break;
	}
}

void PrintFieldValueAsWarning(FieldValue* value, FieldType__Kind kind, int length)
{
	char* timespecBuffer = NULL;
	uint8_t *binaryValues = NULL;
	int iterator = 0;

	switch (kind)
	{
	case FIELD_TYPE__KIND__BOOLEAN:
		elog(WARNING, "%d\n", (int) value->value8);
		break;
	case FIELD_TYPE__KIND__BYTE:
		elog(WARNING,  "%.2X\n", value->value8);
		break;
	case FIELD_TYPE__KIND__SHORT:
	case FIELD_TYPE__KIND__INT:
	case FIELD_TYPE__KIND__LONG:
		elog(WARNING, "%ld\n", value->value64);
		break;
	case FIELD_TYPE__KIND__FLOAT:
		elog(WARNING, "%.2f\n", value->floatValue);
		break;
	case FIELD_TYPE__KIND__DOUBLE:
		elog(WARNING, "%.2lf\n", value->doubleValue);
		break;
	case FIELD_TYPE__KIND__STRING:
		elog(WARNING, "%s\n", value->binary);
		break;
	case FIELD_TYPE__KIND__TIMESTAMP:
		timespecBuffer = alloc(TIMESPEC_BUFFER_LENGTH);
		TimespecToStr(timespecBuffer, &value->time);
		elog(WARNING,  "%s\n", timespecBuffer);
		break;
	case FIELD_TYPE__KIND__BINARY:
		binaryValues = (uint8_t*) value->binary;
		for (iterator = 0; iterator < length; ++iterator)
		{
			elog(WARNING,  "%.2X\n", binaryValues[iterator]);
		}
		break;
	default:
		break;
	}
}

char* getTypeKindName(FieldType__Kind kind)
{
	if (kind == FIELD_TYPE__KIND__BOOLEAN)
		return "BOOLEAN";
	else if (kind == FIELD_TYPE__KIND__BYTE)
		return "BYTE";
	else if (kind == FIELD_TYPE__KIND__SHORT)
		return "SHORT";
	else if (kind == FIELD_TYPE__KIND__INT)
		return "INT";
	else if (kind == FIELD_TYPE__KIND__LONG)
		return "LONG";
	else if (kind == FIELD_TYPE__KIND__FLOAT)
		return "FLOAT";
	else if (kind == FIELD_TYPE__KIND__DOUBLE)
		return "DOUBLE";
	else if (kind == FIELD_TYPE__KIND__STRING)
		return "STRING";
	else if (kind == FIELD_TYPE__KIND__BINARY)
		return "BINARY";
	else if (kind == FIELD_TYPE__KIND__TIMESTAMP)
		return "TIMESTAMP";
	else if (kind == FIELD_TYPE__KIND__LIST)
		return "LIST";
	else if (kind == FIELD_TYPE__KIND__MAP)
		return "MAP";
	else if (kind == FIELD_TYPE__KIND__STRUCT)
		return "STRUCT";
	else if (kind == FIELD_TYPE__KIND__UNION)
		return "UNION";
	else if (kind == FIELD_TYPE__KIND__DECIMAL)
		return "DECIMAL";
	else
		return NULL;
}