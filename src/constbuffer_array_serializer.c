// Copyright (C) Microsoft Corporation. All rights reserved.

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include "azure_c_logging/xlogging.h"

#include "azure_c_pal/gballoc_hl.h"
#include "azure_c_pal/gballoc_hl_redirect.h"
#include "azure_c_util/memory_data.h"

#include "azure_c_util/constbuffer_array_serializer.h"

/*
Header for generic constbuffer_array
| Count of buffers | Size of buffer 1 | Size of buffer 2 | ... | Size of buffer N |
|------------------|------------------|------------------|-----|------------------|
| 4 bytes          | 4 bytes          | 4 bytes          | ... | 4 bytes          |
*/

#define BUFFER_HEADER_COUNT_OFFSET (0)
#define BUFFER_HEADER_COUNT_SIZE (sizeof(uint32_t))

#define BUFFER_HEADER_SIZE_SIZE (sizeof(uint32_t))

#define BUFFER_HEADER_SIZE_START_OFFSET (BUFFER_HEADER_COUNT_OFFSET + BUFFER_HEADER_COUNT_SIZE)

CONSTBUFFER_HANDLE constbuffer_array_serializer_generate_header(CONSTBUFFER_ARRAY_HANDLE data)
{
    CONSTBUFFER_HANDLE result;

    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_001: [ If data is NULL then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
    if (data == NULL)
    {
        LogError("Invalid args: CONSTBUFFER_ARRAY_HANDLE data = %p", data);
        result = NULL;
    }
    else
    {
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_002: [ constbuffer_array_serializer_generate_header shall get the number of buffers in data by calling constbuffer_array_get_buffer_count. ]*/
        uint32_t buffer_count;
        (void)constbuffer_array_get_buffer_count(data, &buffer_count);

        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_003: [ constbuffer_array_serializer_generate_header shall allocate memory to hold the header (with size as 4 + (number of buffers * 4). ]*/
        size_t header_size = BUFFER_HEADER_COUNT_SIZE + (BUFFER_HEADER_SIZE_SIZE * buffer_count);
        if (header_size > UINT32_MAX)
        {
            LogError("entire header size %zu exceeds UINT32_MAX=%" PRIu32 "", header_size, UINT32_MAX);
            result = NULL;
        }
        else
        {
            unsigned char* header_memory = malloc(header_size);

            if (header_memory == NULL)
            {
                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [ If there are any other failures then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
                LogError("Failed to allocate %" PRIu64 " bytes for header", header_size);
                result = NULL;
            }
            else
            {
                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_004: [ constbuffer_array_serializer_generate_header shall set the first 4 bytes in the header buffer to the count of buffers in the array. ]*/
                write_uint32_t(header_memory + BUFFER_HEADER_COUNT_OFFSET, buffer_count);

                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_005: [ For each buffer in data, constbuffer_array_serializer_generate_header shall get the size of the buffer and store it in the header memory. ]*/
                uint32_t i;
                for (i = 0; i < buffer_count; ++i)
                {
                    unsigned char* buffer_size_in_header = header_memory + BUFFER_HEADER_SIZE_START_OFFSET + (BUFFER_HEADER_SIZE_SIZE * i);
                    const CONSTBUFFER* buffer = constbuffer_array_get_buffer_content(data, i);

                    if (buffer->size > UINT32_MAX)
                    {
                        LogError("buffer %" PRIu32 " has size %zu that exceeds UINT32_MAX=%" PRIu32 "", i, buffer->size, UINT32_MAX);
                        break;
                    }
                    write_uint32_t(buffer_size_in_header, (uint32_t)buffer->size);
                }

                if (i != buffer_count)
                {
                    LogError("not all buffers could be processed, processed %" PRIu32 " out of %" PRIu32 "", i, buffer_count);
                    result = NULL;
                }
                else
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_006: [ constbuffer_array_serializer_generate_header shall create a CONSTBUFFER_HANDLE for the header by calling CONSTBUFFER_CreateWithMoveMemory. ]*/
                    CONSTBUFFER_HANDLE header_buffer_temp = CONSTBUFFER_CreateWithMoveMemory(header_memory, header_size);
                    if (header_buffer_temp == NULL)
                    {
                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [ If there are any other failures then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
                        LogError("CONSTBUFFER_CreateWithMoveMemory failed");
                        result = NULL;
                    }
                    else
                    {
                        header_memory = NULL;

                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_008: [ constbuffer_array_serializer_generate_header shall succeed and return the allocated CONSTBUFFER_HANDLE. ]*/
                        result = header_buffer_temp;

                        //CONSTBUFFER_DecRef(header_buffer_temp);
                    }
                }
                if (header_memory != NULL)
                {
                    free(header_memory);
                }
                
            }
        }
    }

    return result;
}

/*returns 0 if source is serializable and outputs in serializedSize the required number of bytes for serialization*/
/*any other value means it cannot be serialized*/
static bool isSerializable(CONSTBUFFER_ARRAY_HANDLE source, uint32_t maximumSizeOfSerialization, uint32_t* nBuffers, uint32_t* serializedSize)
{
    bool result;
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [ constbuffer_array_serialize shall get the number of CONSTBUFFER_HANDLEs inside source. ]*/
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_033: [ constbuffer_array_serialize_with_prepend shall get the number of CONSTBUFFER_HANDLEs inside payload. ]*/
    (void)constbuffer_array_get_buffer_count(source, nBuffers); /*cannot fail*/

    /*avoid some overflows here*/
    if (maximumSizeOfSerialization / sizeof(uint32_t) <= *nBuffers)
    {
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
        LogError("the number of buffers (%" PRIu32 ") is too high!", *nBuffers);
        result = false;
    }
    else
    {
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_034: [ constbuffer_array_serialize_with_prepend shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [ constbuffer_array_serialize shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
        uint32_t sizeOfAllBuffers;
        if (constbuffer_array_get_all_buffers_size(source, &sizeOfAllBuffers) != 0)
        {
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
            LogError("failure in constbuffer_array_get_all_buffers_size");
            result = false;
        }
        else
        {
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [ constbuffer_array_serialize shall ensure that the total serialized size does not exceed UINT32_MAX. ]*/
            *serializedSize = sizeof(uint32_t); /*nBuffers*/

            *serializedSize += (*nBuffers * sizeof(uint32_t));
            if (*serializedSize > maximumSizeOfSerialization - sizeOfAllBuffers)
            {
                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
                LogError("serializing %" PRIu32 " buffers that have in total %" PRIu32 " bytes exceeds %" PRIu32 "", *nBuffers, sizeOfAllBuffers, maximumSizeOfSerialization);
                result = false;
            }
            else
            {
                *serializedSize += sizeOfAllBuffers;
                result = true;
            }
        }
    }
    return result;
}

/*format for serialize/deserialize*/
/*
| Count of buffers | Size of buffer 1 | Size of buffer 2 | ... | Size of buffer N | Buffer 1 | Buffer 2 | ... | Buffer N |
|------------------|------------------|------------------|-----|------------------|----------|----------|-----|----------|
| 4 bytes          | 4 bytes          | 4 bytes          | ... | 4 bytes          | variable | variable | ... | variable |
*/


static void constbuffer_array_serialize_into_byte_array(unsigned char* destination, uint32_t nBuffers, CONSTBUFFER_ARRAY_HANDLE source)
{
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [ constbuffer_array_serialize shall write at offset 0 the number of buffers. ]*/
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_039: [ constbuffer_array_serialize_with_prepend shall write at the next offset the number of buffers in payload ]*/
    write_uint32_t(destination, nBuffers);

    uint32_t iBuffer;
    unsigned char* whereToWriteBufferContent = destination + sizeof(uint32_t) + nBuffers * sizeof(uint32_t);
    unsigned char* whereToWriteBufferSize = destination + sizeof(uint32_t);
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [ constbuffer_array_serialize shall write at consecutive offsets the size of each buffer. ]*/
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [ constbuffer_array_serialize shall write at consecutive offsets the content of buffers. ]*/
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_040: [ constbuffer_array_serialize_with_prepend shall write at the next consecutive offsets the size of each buffer of payload ]*/
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_041: [ constbuffer_array_serialize_with_prepend shall write at consecutive offsets the content of payload's buffers. ]*/
    for (iBuffer = 0; iBuffer < nBuffers; iBuffer++)
    {
        const CONSTBUFFER* bufferContent = constbuffer_array_get_buffer_content(source, iBuffer);
        write_uint32_t(whereToWriteBufferSize, (uint32_t)bufferContent->size);
        (void)memcpy(whereToWriteBufferContent, bufferContent->buffer, bufferContent->size);

        whereToWriteBufferSize += sizeof(uint32_t);
        whereToWriteBufferContent += bufferContent->size;
    }
}
   

CONSTBUFFER_HANDLE constbuffer_array_serialize(CONSTBUFFER_ARRAY_HANDLE source)
{
    CONSTBUFFER_HANDLE result;
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_001: [ If source is NULL then constbuffer_array_serialize shall fail and return NULL. ]*/
    if (source == NULL)
    {
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
        LogError("invalid argument CONSTBUFFER_ARRAY_HANDLE source=%p", source);
        result = NULL;
    }
    else
    {
        uint32_t serializedSize, nBuffers;
        uint32_t maxPossiblePayload = UINT32_MAX - (4096 - 1);/*this complete API will get removed*/
        if (!isSerializable(source, maxPossiblePayload, &nBuffers, &serializedSize))
        {
            LogError("source=%p is not serializable", source);
            result = NULL;
        }
        else
        {
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [ constbuffer_array_serialize shall allocate enough memory to hold all the serialized bytes. ]*/
            unsigned char* destination = malloc(serializedSize);
            if (destination == NULL)
            {
                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
                LogError("failure in malloc");
                result = NULL;
            }
            else
            {
                constbuffer_array_serialize_into_byte_array(destination, nBuffers, source);

                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [ constbuffer_array_serialize shall call CONSTBUFFER_CreateWithMoveMemory from the allocated and written memory. ]*/
                result = CONSTBUFFER_CreateWithMoveMemory(destination, serializedSize);
                if (result == NULL)
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
                    LogError("failure in CONSTBUFFER_CreateWithMoveMemory");
                    /*return as is*/
                }
                else
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [ constbuffer_array_serialize shall succeed and return a non-zero value. ]*/
                    /*return as is*/
                    goto allOk;
                }
                free(destination);
            }
        }
allOk:;
    }
    return result;
}

static void dec_ref_constbuffer(void* context)
{
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_028: [ dec_ref_constbuffer shall call CONSTBUFFER_DecRef on context. ]*/
    CONSTBUFFER_HANDLE source = context; /*context is "source" passed to constbuffer_array_deserialize*/
    CONSTBUFFER_DecRef(source);
}

CONSTBUFFER_ARRAY_HANDLE constbuffer_array_deserialize(CONSTBUFFER_HANDLE source)
{
    CONSTBUFFER_ARRAY_HANDLE result;
    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_012: [ If source is NULL then constbuffer_array_deserialize shall fail and return NULL. ]*/
    if (source == NULL)
    {
        LogError("invalid argument CONSTBUFFER_HANDLE source=%p", source);
        result = NULL;
    }
    else
    {
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_013: [ constbuffer_array_deserialize shall get the buffer content by a call to CONSTBUFFER_GetContent. ]*/
        const CONSTBUFFER* content = CONSTBUFFER_GetContent(source);
        /*basic verifications*/
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_014: [ If the source size is smaller than sizeof(uint32_t) then constbuffer_array_deserialize shall fail and return NULL. ]*/
        if (content->size < sizeof(uint32_t))
        {
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
            LogError("source does not have enough bytes to read the number of buffers");
            result = NULL;
        }
        else
        {
            uint32_t nBuffers;
            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_015: [ constbuffer_array_deserialize shall read a uint32_t from source that is the number of buffers (nBuffers). ]*/
            read_uint32_t(content->buffer, &nBuffers);

            if (nBuffers == 0)
            {
                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_016: [ If nBuffers is 0 then constbuffer_array_deserialize shall verify that source content does not contain other bytes. ]*/
                /*apparently this is a valid case when an empty constbuffer_array is serialized*/
                if (content->size != sizeof(uint32_t))
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                    LogError("content size (%zu)does not match the expected size of %zu", content->size, sizeof(uint32_t));
                    result = NULL;
                }
                else
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_017: [ If nBuffers is 0 then constbuffer_array_deserialize shall call constbuffer_array_create_empty, succeed, and return a non-NULL value. ]*/
                    result = constbuffer_array_create_empty();
                    if (result == NULL)
                    {
                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                        LogError("failure in constbuffer_array_create_empty");
                        /*return as is*/
                    }
                    else
                    {
                        /*return as is*/
                    }
                }
            }
            else
            {
                if (nBuffers > (UINT32_MAX - sizeof(uint32_t)) / sizeof(uint32_t))
                {
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                    LogError("too many buffers (%" PRIu32 ")", nBuffers);
                    result = NULL;
                }
                else
                {
                    uint32_t iBuffer;
                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_018: [ constbuffer_array_deserialize shall verify that source contains sufficient bytes to read nBuffers uint32_t values that are individual buffer sizes. ]*/
                    uint32_t totalComputedSize = sizeof(uint32_t) + nBuffers * sizeof(uint32_t); /*totalComputedSize tracks the discovered size of the buffer*/;
                    if (totalComputedSize > content->size)
                    {
                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                        LogError("would not be able to read %" PRIu32 " buffers when the whole CONSTBUFFER has %zu bytes available", nBuffers, content->size);
                        result = NULL;
                    }
                    else
                    {
                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
                        const unsigned char* whereToReadBufferSize = content->buffer + sizeof(uint32_t); /*first buffer size is read after the number of buffers*/
                        for (iBuffer = 0; iBuffer < nBuffers; iBuffer++)
                        {
                            uint32_t bufferSize;
                            read_uint32_t(whereToReadBufferSize, &bufferSize);
                            if (totalComputedSize > UINT32_MAX - bufferSize)
                            {
                                /*overflow*/
                                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                LogError("while trying to add buffer %" PRIu32 " size (%" PRIu32 "to the already computed size of %" PRIu32 " an overflow was encountered", iBuffer, bufferSize, totalComputedSize);
                                break;
                            }
                            else
                            {
                                totalComputedSize += bufferSize;
                            }
                            whereToReadBufferSize += sizeof(uint32_t);
                        }

                        if (iBuffer != nBuffers)
                        {
                            LogError("not all buffers could be processed");
                            result = NULL;
                        }
                        else
                        {
                            if (totalComputedSize != content->size)
                            {
                                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                LogError("mismatch in expected sizes (expected=%" PRIu32 " while the CONSTBUFFER_HANDLE size was %zu", totalComputedSize, content->size);
                                result = NULL;
                            }
                            else
                            {
                                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_020: [ constbuffer_array_deserialize shall allocate memory to hold nBuffers CONSTBUFFER_HANDLEs. ]*/
                                CONSTBUFFER_HANDLE* allHandles = malloc(nBuffers * sizeof(CONSTBUFFER_HANDLE));
                                if (allHandles == NULL)
                                {
                                    /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                    LogError("failure in malloc");
                                    result = NULL;
                                }
                                else
                                {
                                    whereToReadBufferSize = content->buffer + sizeof(uint32_t);
                                    const unsigned char* whereToReadBufferContent = content->buffer + sizeof(uint32_t) + nBuffers * sizeof(uint32_t);
                                    /*at this moment there's a certainty that the momery area matches as far as size goes*/
                                    for (iBuffer = 0; iBuffer < nBuffers; iBuffer++)
                                    {
                                        uint32_t bufferSize;
                                        read_uint32_t(whereToReadBufferSize, &bufferSize);
                                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_021: [ constbuffer_array_deserialize shall inc_ref source for every buffer that it constructs. ]*/
                                        CONSTBUFFER_IncRef(source);
                                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_029: [ constbuffer_array_deserialize shall construct nBuffers CONSTBUFFER_HANDLE by calls to CONSTBUFFER_CreateWithCustomFree with customFreeFunc parameter set to dec_ref_constbuffer ]*/
                                        allHandles[iBuffer] = CONSTBUFFER_CreateWithCustomFree(whereToReadBufferContent, bufferSize, dec_ref_constbuffer, source);
                                        if (allHandles[iBuffer] == NULL)
                                        {
                                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                            CONSTBUFFER_DecRef(source);
                                            LogError("failure in CONSTBUFFER_CreateWithCustomFree");
                                            break;
                                        }
                                        else
                                        {
                                            whereToReadBufferSize += sizeof(uint32_t);
                                            whereToReadBufferContent += bufferSize;
                                            /*keep going*/
                                        }
                                    }

                                    if (iBuffer != nBuffers)
                                    {
                                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                        result = NULL;
                                        LogError("not all buffers could be processed");
                                        for (uint32_t jBuffer = 0; jBuffer < iBuffer; jBuffer++)
                                        {
                                            CONSTBUFFER_DecRef(allHandles[jBuffer]);
                                        }
                                    }
                                    else
                                    {
                                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_023: [ constbuffer_array_create shall call constbuffer_array_create passing the previously constructed CONSTBUFFER_HANDLE array. ]*/
                                        result = constbuffer_array_create(allHandles, nBuffers);
                                        if (result == NULL)
                                        {
                                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
                                            LogError("failure in constbuffer_array_create");
                                            /*return as is*/
                                        }
                                        else
                                        {
                                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_024: [ constbuffer_array_create shall succeed and return a non-NULL value. ]*/
                                            /*return as is*/
                                        }

                                        for (uint32_t jBuffer = 0; jBuffer < nBuffers; jBuffer++)
                                        {
                                            CONSTBUFFER_DecRef(allHandles[jBuffer]);
                                        }
                                    }
                                    free(allHandles);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return result;
}


/*
Metadata bytes | Count of buffers | Size of buffer 1 | Size of buffer 2 | ... | Size of buffer N | Buffer 1 | Buffer 2 | ... | Buffer N |  padding bytes |
---------------|------------------|------------------|------------------|-----|------------------|----------|----------|-----|----------|----------------|
 "n" bytes     | 4 bytes          | 4 bytes          | 4 bytes          | ... | 4 bytes          | variable | variable | ... | variable |unfilled content|
                                                                                                                                                         ^
                                                                                                                                                         |
                                                                                                                                                         -------physical_sector_size multiple
*/

/*produces a CONSTBUFFER that has at the beginning "metadata"->Content->buffer, followed by the serialized form of payload, followed by just enough bytes to complete a physical sector size*/
CONSTBUFFER_HANDLE constbuffer_array_serialize_with_prepend(CONSTBUFFER_HANDLE metadata, CONSTBUFFER_ARRAY_HANDLE payload, uint32_t physical_sector_size, uint32_t* padding_bytes)
{
    CONSTBUFFER_HANDLE result;
    if (
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_030: [ If metadata is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
        (metadata == NULL) ||
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_031: [ If payload is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
        (payload == NULL) ||
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_047: [ If physical_sector_size is 0 then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
        (physical_sector_size == 0) ||
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_048: [ If padding_bytes is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
        (padding_bytes == NULL)
        )
    {
        LogError("invalid arguments constbuffer_array_serialize_with_prepend(CONSTBUFFER_HANDLE metadata=%p, CONSTBUFFER_ARRAY_HANDLE payload=%p, uint32_t physical_sector_size=%" PRIu32 ", uint32_t* padding_bytes=%p)",
            metadata, payload, physical_sector_size, padding_bytes);
        result = NULL;
    }
    else
    {
        uint32_t maximumSizeOfSerialization = UINT32_MAX - (physical_sector_size - 1); /*always yields a multiple of physical_sector_size, and this is the greatest multiple before UINT32_MAX*/
        const CONSTBUFFER* metadataContent = CONSTBUFFER_GetContent(metadata);
        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
        if (metadataContent->size >= UINT32_MAX)
        {
            LogError("metadataContent->size=%zu >= UINT32_MAX=%" PRIu32 "",
                metadataContent->size, UINT32_MAX);
            result = NULL;
        }
        else
        {
            uint32_t metadataSize = (uint32_t)metadataContent->size;

            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
            if (metadataSize >= maximumSizeOfSerialization)
            {
                LogError("invalid arguments metadataSize=%" PRIu32 " > maximumSizeOfSerialization=%" PRIu32 "",
                    metadataSize, maximumSizeOfSerialization);
                result = NULL;
            }
            else
            {
                uint32_t payloadSerializedSize;
                uint32_t payloadNBuffers;
                if (!isSerializable(payload, maximumSizeOfSerialization , &payloadNBuffers, &payloadSerializedSize))
                {
                    LogError("failure in isSerializable(payload=%p, &payloadNBuffers=%p, &payloadSerializedSize=%p)",
                        payload, &payloadNBuffers, &payloadSerializedSize);
                    result = NULL;
                }
                else
                {
                    
                    if (payloadSerializedSize > maximumSizeOfSerialization - metadataSize)
                    {
                        LogError("invalid arguments payloadSerializedSize=%" PRIu32 " > maximumSizeOfSerialization=%" PRIu32 " - metadataSize=%" PRIu32 "",
                            payloadSerializedSize, maximumSizeOfSerialization, metadataSize);
                        result = NULL;
                    }
                    else
                    {
                        uint32_t rem = (metadataSize + payloadSerializedSize) % physical_sector_size;
                        uint32_t paddingBytes;
                        if (rem == 0)
                        {
                            /*no padding bytes needed*/
                            paddingBytes = 0;
                        }
                        else
                        {
                            /*some padding bytes needed*/
                            paddingBytes = (physical_sector_size - rem);
                        }

                        uint32_t serializationSize = metadataSize + payloadSerializedSize + paddingBytes;
                        /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_050: [ constbuffer_array_serialize_with_prepend shall allocate memory to hold the complete serialization size that uses padding_bytes to reach a multiple of physical_sector_size. ]*/
                        unsigned char* destination = malloc(serializationSize);
                        if (destination == NULL)
                        {
                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_044: [ If there are any failures then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
                            LogError("failure in malloc(serializationSize=%" PRIu32 ")",
                                serializationSize);
                            result = NULL;
                        }
                        else
                        {
                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_038: [ constbuffer_array_serialize_with_prepend shall copy the memory of metadata to the first bytes of the allocated memory. ]*/
                            (void)memcpy(destination + 0, metadataContent->buffer, metadataSize);

                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_039: [ constbuffer_array_serialize_with_prepend shall write at the next offset the number of buffers in payload ]*/
                            constbuffer_array_serialize_into_byte_array(destination + metadataSize, payloadNBuffers, payload);

                            /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_042: [ constbuffer_array_serialize_with_prepend shall call CONSTBUFFER_CreateWithMoveMemory. ]*/
                            result = CONSTBUFFER_CreateWithMoveMemory(destination, serializationSize);
                            if (result == NULL)
                            {
                                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_044: [ If there are any failures then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
                                LogError("failure in CONSTBUFFER_CreateWithMoveMemory(destination=%p, serializationSize=%" PRIu32 ");",
                                    destination, serializationSize);
                                /*return as is*/
                            }
                            else
                            {
                                *padding_bytes = paddingBytes;
                                /*Codes_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_049: [ constbuffer_array_serialize_with_prepend shall succeed, write in padding_bytes the number of padding bytes it produced and return a non-NULL value. ]*/
                                goto allOk;
                            }
                            free(destination);
                        }
                    allOk:;
                    }
                }
            }
        }
    }
    return result;
}
