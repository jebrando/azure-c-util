`constbuffer_array_serializer` requirements
================

## Overview

This module is a helper to serialize a `CONSTBUFFER_ARRAY_HANDLE` into a flattened `CONSTBUFFER_HANDLE` by creating a header to describe the format of the array.

The generated header has the following format:

```code
| Count of buffers | Size of buffer 1 | Size of buffer 2 | ... | Size of buffer N |
|------------------|------------------|------------------|-----|------------------|
| 4 bytes          | 4 bytes          | 4 bytes          | ... | 4 bytes          |
```

The serialization format is as follows: 

```code
| Count of buffers | Size of buffer 1 | Size of buffer 2 | ... | Size of buffer N | Buffer 1 | Buffer 2 | ... | Buffer N |  padding bytes |
|------------------|------------------|------------------|-----|------------------|----------|----------|-----|----------|----------------|
| 4 bytes          | 4 bytes          | 4 bytes          | ... | 4 bytes          | variable | variable | ... | variable |unfilled content|
```

## Exposed API

```c
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serializer_generate_header, CONSTBUFFER_ARRAY_HANDLE, data);
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize, CONSTBUFFER_ARRAY_HANDLE, source);
MOCKABLE_FUNCTION(, CONSTBUFFER_ARRAY_HANDLE, constbuffer_array_deserialize, CONSTBUFFER_HANDLE, source);
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize_with_prepend, CONSTBUFFER_HANDLE, metadata, CONSTBUFFER_ARRAY_HANDLE, payload, uint32_t, physical_sector_size, uint32_t*, padding_bytes);
```

### constbuffer_array_serializer_generate_header

```c
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serializer_generate_header, CONSTBUFFER_ARRAY_HANDLE, data);
```

Generate a header to describe a flattened const buffer array. This generates a header which describes the number of buffers in the array and the size of each buffer so that the buffers could be written serially and later unserialized into the original array structure.

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_001: [** If `data` is `NULL` then `constbuffer_array_serializer_generate_header` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_002: [** `constbuffer_array_serializer_generate_header` shall get the number of buffers in `data` by calling `constbuffer_array_get_buffer_count`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_003: [** `constbuffer_array_serializer_generate_header` shall allocate memory to hold the header (with size as `4 + (number of buffers * 4)`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_004: [** `constbuffer_array_serializer_generate_header` shall set the first 4 bytes in the header buffer to the count of buffers in the array. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_005: [** For each buffer in `data`, `constbuffer_array_serializer_generate_header` shall get the size of the buffer and store it in the header memory. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_006: [** `constbuffer_array_serializer_generate_header` shall create a `CONSTBUFFER_HANDLE` for the header by calling `CONSTBUFFER_CreateWithMoveMemory`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [** If there are any other failures then `constbuffer_array_serializer_generate_header` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_008: [** `constbuffer_array_serializer_generate_header` shall succeed and return the allocated `CONSTBUFFER_HANDLE`. **]**

### constbuffer_array_serialize
```c
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize, CONSTBUFFER_ARRAY_HANDLE, source);
```

`constbuffer_array_serialize` produces a `CONSTBUFFER_HANDLE` that conforms to serialization format.

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_001: [** If `source` is `NULL` then `constbuffer_array_serialize` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [** `constbuffer_array_serialize` shall get the number of `CONSTBUFFER_HANDLE`s inside `source`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [** `constbuffer_array_serialize` shall get the size of all the buffers by a call to `constbuffer_array_get_all_buffers_size`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [** `constbuffer_array_serialize` shall ensure that the total serialized size does not exceed `UINT32_MAX`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [** `constbuffer_array_serialize` shall allocate enough memory to hold all the serialized bytes. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [** `constbuffer_array_serialize` shall write at offset 0 the number of buffers. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [** `constbuffer_array_serialize` shall write at consecutive offsets the size of each buffer. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [** `constbuffer_array_serialize` shall write at consecutive offsets the content of buffers. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [** `constbuffer_array_serialize` shall call `CONSTBUFFER_CreateWithMoveMemory` from the allocated and written memory. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [** `constbuffer_array_serialize` shall succeed and return a non-zero value. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [** If there are any failures then `constbuffer_array_serialize` shall fail and return `NULL`. **]**


### constbuffer_array_deserialize
```c
MOCKABLE_FUNCTION(, CONSTBUFFER_ARRAY_HANDLE, constbuffer_array_deserialize, CONSTBUFFER_HANDLE, source);
```

`constbuffer_array_deserialize` produces a `CONSTBUFFER_ARRAY_HANDLE` from `source` that conforms to serialization format.

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_012: [** If `source` is `NULL` then `constbuffer_array_deserialize` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_013: [** `constbuffer_array_deserialize` shall get the buffer content by a call to `CONSTBUFFER_GetContent`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_014: [** If the `source` size is smaller than `sizeof(uint32_t)` then `constbuffer_array_deserialize` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_015: [** `constbuffer_array_deserialize` shall read a `uint32_t` from `source` that is the number of buffers (`nBuffers`). **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_016: [** If `nBuffers` is 0 then `constbuffer_array_deserialize` shall verify that `source` content does not contain other bytes. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_017: [** If `nBuffers` is 0 then `constbuffer_array_deserialize` shall call `constbuffer_array_create_empty`, succeed, and return a non-`NULL` value. **]**

Following requirements apply when `nBuffers` is greater than 0.

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_018: [** `constbuffer_array_deserialize` shall verify that `source` contains sufficient bytes to read `nBuffers` `uint32_t` values that are individual buffer sizes. **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [** `constbuffer_array_deserialize` shall verify that `source` contains exactly sufficient bytes to read `nBuffers` individual buffer sizes. **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_020: [** `constbuffer_array_deserialize` shall allocate memory to hold `nBuffers` `CONSTBUFFER_HANDLE`s. **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_021: [** `constbuffer_array_deserialize` shall inc_ref `source` for every buffer that it constructs. **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_029: [** `constbuffer_array_deserialize` shall construct `nBuffers` `CONSTBUFFER_HANDLE` by calls to `CONSTBUFFER_CreateWithCustomFree` with `customFreeFunc` parameter set to `dec_ref_constbuffer` **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_023: [** `constbuffer_array_create` shall call `constbuffer_array_create` passing the previously constructed `CONSTBUFFER_HANDLE` array. **]**

   **SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_024: [** `constbuffer_array_create` shall succeed and return a non-`NULL` value. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [** If there are any failures then `constbuffer_array_create` shall fail and return `NULL`. **]**


### dec_ref_constbuffer
```c
static void dec_ref_constbuffer(void* context)
```

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_028: [** `dec_ref_constbuffer` shall call `CONSTBUFFER_DecRef` on `context`. **]**

### constbuffer_array_serialize_with_prepend
```c
MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize_with_prepend, CONSTBUFFER_HANDLE, metadata, CONSTBUFFER_ARRAY_HANDLE, payload, uint32_t, physical_sector_size, uint32_t*, padding_bytes);
```

`constbuffer_array_serialize_with_prepend` produces a `CONSTBUFFER_HANDLE` that contains as first bytes the content of `metadata` and as following bytes the serialized form of `payload`.


**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_030: [** If `metadata` is `NULL` then `constbuffer_array_serialize_with_prepend` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_031: [** If `payload` is `NULL` then `constbuffer_array_serialize_with_prepend` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_047: [** If `physical_sector_size` is 0 then `constbuffer_array_serialize_with_prepend` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_048: [** If `padding_bytes` is `NULL` then `constbuffer_array_serialize_with_prepend` shall fail and return `NULL`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [** `constbuffer_array_serialize_with_prepend` shall ensure that the total serialized size does not exceed `UINT32_MAX` - (`physical_sector_size`-1). **]** Explanation:(`UINT32_MAX` - (`physical_sector_size`-1) is the greatest multiple of `physical_sector_size` that is still smaller than `UINT32_MAX`).

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_033: [** `constbuffer_array_serialize_with_prepend` shall get the number of `CONSTBUFFER_HANDLE`s inside `payload`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_034: [** `constbuffer_array_serialize_with_prepend` shall get the size of all the buffers by a call to `constbuffer_array_get_all_buffers_size`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_050: [** `constbuffer_array_serialize_with_prepend` shall allocate memory to hold the complete serialization size that uses padding_bytes to reach a multiple of `physical_sector_size`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_038: [** `constbuffer_array_serialize_with_prepend` shall copy the memory of metadata to the first bytes of the allocated memory. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_039: [** `constbuffer_array_serialize_with_prepend` shall write at the next offset the number of buffers in `payload` **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_040: [** `constbuffer_array_serialize_with_prepend` shall write at the next consecutive offsets the size of each buffer of `payload` **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_041: [** `constbuffer_array_serialize_with_prepend` shall write at consecutive offsets the content of `payload`'s buffers. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_042: [** `constbuffer_array_serialize_with_prepend` shall call `CONSTBUFFER_CreateWithMoveMemory`. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_049: [** `constbuffer_array_serialize_with_prepend` shall succeed, write in `padding_bytes` the number of padding bytes it produced and return a non-`NULL` value. **]**

**SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_044: [** If there are any failures then `constbuffer_array_serialize_with_prepend` shall fail and return `NULL`. **]**



