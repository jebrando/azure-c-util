// Copyright (c) Microsoft. All rights reserved.

#ifdef __cplusplus
#include <cstdlib>
#include <cinttypes>
#else
#include <stdlib.h>
#include <inttypes.h>
#endif

#include "azure_macro_utils/macro_utils.h"

#include "real_gballoc_ll.h"
static void* my_gballoc_malloc(size_t size)
{
    return real_gballoc_ll_malloc(size);
}

static void my_gballoc_free(void* ptr)
{
     real_gballoc_ll_free(ptr);
}

#include "testrunnerswitcher.h"
#include "umock_c/umock_c.h"
#include "umock_c/umocktypes_stdint.h"
#include "umock_c/umocktypes.h"
#include "umock_c/umock_c_negative_tests.h"

#include "azure_c_pal/interlocked.h" /*included for mocking reasons - it will prohibit creation of mocks belonging to interlocked.h - at the moment verified through int tests - this is porting legacy code, temporary solution*/

#define ENABLE_MOCKS
#include "azure_c_pal/gballoc_hl.h"
#include "azure_c_pal/gballoc_hl_redirect.h"

#include "azure_c_util/constbuffer.h"
#include "azure_c_util/constbuffer_array.h"
#include "azure_c_util/memory_data.h"

#include "crc32c.h"
#undef ENABLE_MOCKS

#include "real_gballoc_hl.h"


#include "../reals/real_constbuffer.h"
#include "../reals/real_constbuffer_array.h"
#include "../reals/real_memory_data.h"

#include "constbuffer_array_serializer.h"

static TEST_MUTEX_HANDLE test_serialize_mutex;

MU_DEFINE_ENUM_STRINGS(UMOCK_C_ERROR_CODE, UMOCK_C_ERROR_CODE_VALUES)

static void on_umock_c_error(UMOCK_C_ERROR_CODE error_code)
{
    ASSERT_FAIL("umock_c reported error :%" PRI_MU_ENUM "", MU_ENUM_VALUE(UMOCK_C_ERROR_CODE, error_code));
}

#define TEST_PHYSICAL_SECTOR_SIZE_DEFINE 4096
static const uint32_t TEST_PHYSICAL_SECTOR_SIZE = TEST_PHYSICAL_SECTOR_SIZE_DEFINE;

static CONSTBUFFER_HANDLE generate_test_buffer(uint32_t size, unsigned char data)
{
    unsigned char* memory = (unsigned char*)my_gballoc_malloc(size);
    ASSERT_IS_NOT_NULL(memory);

    (void)memset(memory, data, size);

    CONSTBUFFER_HANDLE result = real_CONSTBUFFER_CreateWithMoveMemory(memory, size);
    ASSERT_IS_NOT_NULL(result);

    return result;
}

// Creates CONSTBUFFER_ARRAY where each buffer is of size buffer_size, buffer_size+size_increment, buffer_size+(size_increment*2), ... buffer_size+(size_increment*N)
// size_increment may be negative but test fails if it results in a negative buffer_size
static CONSTBUFFER_ARRAY_HANDLE generate_test_buffer_array_increasing_size(uint32_t buffer_count, uint32_t buffer_size, int32_t size_increment)
{
    CONSTBUFFER_HANDLE* buffers = (CONSTBUFFER_HANDLE*)my_gballoc_malloc(sizeof(CONSTBUFFER_HANDLE) * buffer_count);
    ASSERT_IS_NOT_NULL(buffers);

    for (uint32_t i = 0; i < buffer_count; ++i)
    {
        uint32_t this_buffer_size = (uint32_t)((int32_t)buffer_size + size_increment * i);
        buffers[i] = generate_test_buffer(this_buffer_size, 'a'+(i % ('z' - 'a')));
    }

    CONSTBUFFER_ARRAY_HANDLE buffer_array = real_constbuffer_array_create(buffers, buffer_count);
    ASSERT_IS_NOT_NULL(buffer_array);

    // cleanup

    for (uint32_t i = 0; i < buffer_count; ++i)
    {
        real_CONSTBUFFER_DecRef(buffers[i]);
    }
    my_gballoc_free(buffers);

    return buffer_array;
}

static CONSTBUFFER_ARRAY_HANDLE generate_test_buffer_array(uint32_t buffer_count, uint32_t buffer_size)
{
    return generate_test_buffer_array_increasing_size(buffer_count, buffer_size, 0);
}

BEGIN_TEST_SUITE(constbuffer_array_serializer_unittests)

TEST_SUITE_INITIALIZE(suite_init)
{
    ASSERT_ARE_EQUAL(int, 0, real_gballoc_hl_init(NULL, NULL));
    test_serialize_mutex = TEST_MUTEX_CREATE();
    ASSERT_IS_NOT_NULL(test_serialize_mutex);

    ASSERT_ARE_EQUAL(int, 0, umock_c_init(on_umock_c_error), "umock_c_init");

    ASSERT_ARE_EQUAL(int, 0, umocktypes_stdint_register_types(), "umocktypes_stdint_register_types");

    REGISTER_GLOBAL_MOCK_HOOK(malloc, my_gballoc_malloc);
    REGISTER_GLOBAL_MOCK_HOOK(free, my_gballoc_free);

    REGISTER_GLOBAL_MOCK_FAIL_RETURN(malloc, NULL);
    REGISTER_GLOBAL_MOCK_FAIL_RETURN(CONSTBUFFER_CreateWithMoveMemory, NULL);

    REGISTER_CONSTBUFFER_GLOBAL_MOCK_HOOK();
    REGISTER_CONSTBUFFER_ARRAY_GLOBAL_MOCK_HOOK();
    REGISTER_GLOBAL_MOCK_FAIL_RETURN(constbuffer_array_get_buffer_count, MU_FAILURE);
    REGISTER_GLOBAL_MOCK_FAIL_RETURN(constbuffer_array_get_all_buffers_size, MU_FAILURE);

    REGISTER_MEMORY_DATA_GLOBAL_MOCK_HOOK();

    REGISTER_UMOCK_ALIAS_TYPE(CONSTBUFFER_ARRAY_HANDLE, void*);
    REGISTER_UMOCK_ALIAS_TYPE(const CONSTBUFFER_ARRAY_HANDLE, void*);
    REGISTER_UMOCK_ALIAS_TYPE(CONSTBUFFER_HANDLE, void*);
    REGISTER_UMOCK_ALIAS_TYPE(CONSTBUFFER_CUSTOM_FREE_FUNC, void*);
    
}

TEST_SUITE_CLEANUP(suite_cleanup)
{
    umock_c_deinit();

    TEST_MUTEX_DESTROY(test_serialize_mutex);

    real_gballoc_hl_deinit();
}

TEST_FUNCTION_INITIALIZE(method_init)
{
    if (TEST_MUTEX_ACQUIRE(test_serialize_mutex))
    {
        ASSERT_FAIL("Could not acquire test serialization mutex.");
    }

    umock_c_reset_all_calls();
    umock_c_negative_tests_init();
}

TEST_FUNCTION_CLEANUP(method_cleanup)
{
    umock_c_negative_tests_deinit();
    TEST_MUTEX_RELEASE(test_serialize_mutex);
}

//
// constbuffer_array_serializer_generate_header
//

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_001: [ If data is NULL then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_null_data_fails)
{
    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(NULL);

    /// assert
    ASSERT_IS_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_002: [ constbuffer_array_serializer_generate_header shall get the number of buffers in data by calling constbuffer_array_get_buffer_count. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_003: [ constbuffer_array_serializer_generate_header shall allocate memory to hold the header (with size as 4 + (number of buffers * 4). ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_004: [ constbuffer_array_serializer_generate_header shall set the first 4 bytes in the header buffer to the count of buffers in the array. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_006: [ constbuffer_array_serializer_generate_header shall create a CONSTBUFFER_HANDLE for the header by calling CONSTBUFFER_CreateWithMoveMemory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_008: [ constbuffer_array_serializer_generate_header shall succeed and return the allocated CONSTBUFFER_HANDLE. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_empty_array_works)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = real_constbuffer_array_create_empty();

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(sizeof(uint32_t)));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 0));

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, sizeof(uint32_t)));

    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

    /// assert
    ASSERT_IS_NOT_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    const CONSTBUFFER* header_data = real_CONSTBUFFER_GetContent(header);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t), header_data->size);

    uint32_t buffer_count;
    real_read_uint32_t(header_data->buffer, &buffer_count);
    ASSERT_ARE_EQUAL(uint32_t, 0, buffer_count);

    /// cleanup
    real_CONSTBUFFER_DecRef(header);
    real_constbuffer_array_dec_ref(data);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_002: [ constbuffer_array_serializer_generate_header shall get the number of buffers in data by calling constbuffer_array_get_buffer_count. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_003: [ constbuffer_array_serializer_generate_header shall allocate memory to hold the header (with size as 4 + (number of buffers * 4). ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_004: [ constbuffer_array_serializer_generate_header shall set the first 4 bytes in the header buffer to the count of buffers in the array. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_005: [ For each buffer in data, constbuffer_array_serializer_generate_header shall get the size of the buffer and store it in the header memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_006: [ constbuffer_array_serializer_generate_header shall create a CONSTBUFFER_HANDLE for the header by calling CONSTBUFFER_CreateWithMoveMemory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_008: [ constbuffer_array_serializer_generate_header shall succeed and return the allocated CONSTBUFFER_HANDLE. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_1_buffer_works)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = generate_test_buffer_array(1, 42);

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(sizeof(uint32_t) + sizeof(uint32_t)));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 1));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 0));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 42));

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, sizeof(uint32_t) + sizeof(uint32_t)));

    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

    /// assert
    ASSERT_IS_NOT_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    const CONSTBUFFER* header_data = real_CONSTBUFFER_GetContent(header);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t) + sizeof(uint32_t), header_data->size);

    uint32_t buffer_count;
    real_read_uint32_t(header_data->buffer, &buffer_count);
    ASSERT_ARE_EQUAL(uint32_t, 1, buffer_count);

    uint32_t buffer_size;
    real_read_uint32_t(header_data->buffer + sizeof(uint32_t), &buffer_size);
    ASSERT_ARE_EQUAL(uint32_t, 42, buffer_size);

    /// cleanup
    real_CONSTBUFFER_DecRef(header);
    real_constbuffer_array_dec_ref(data);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [ If there are any other failures then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_1_buffer_that_is_greater_than_UINT32_MAX_fails)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = generate_test_buffer_array(1, 42);

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(sizeof(uint32_t) + sizeof(uint32_t)));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 1));

    CONSTBUFFER tooBigOfABufferSize;
    tooBigOfABufferSize.buffer = NULL;
    tooBigOfABufferSize.size = (size_t)UINT32_MAX + 1;

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 0))
        .SetReturn(&tooBigOfABufferSize);

    STRICT_EXPECTED_CALL(free(IGNORED_ARG));

    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

    /// assert
    ASSERT_IS_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    /// cleanup
    real_constbuffer_array_dec_ref(data);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [ If there are any other failures then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_too_many_arraybuffers_fails)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = generate_test_buffer_array(1, 42);

    uint32_t tooManyBuffers = UINT32_MAX / sizeof(uint32_t);

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG))
        .CopyOutArgumentBuffer_buffer_count(&tooManyBuffers, sizeof(tooManyBuffers));

    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

    /// assert
    ASSERT_IS_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_constbuffer_array_dec_ref(data);
}


/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_002: [ constbuffer_array_serializer_generate_header shall get the number of buffers in data by calling constbuffer_array_get_buffer_count. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_003: [ constbuffer_array_serializer_generate_header shall allocate memory to hold the header (with size as 4 + (number of buffers * 4). ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_004: [ constbuffer_array_serializer_generate_header shall set the first 4 bytes in the header buffer to the count of buffers in the array. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_005: [ For each buffer in data, constbuffer_array_serializer_generate_header shall get the size of the buffer and store it in the header memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_006: [ constbuffer_array_serializer_generate_header shall create a CONSTBUFFER_HANDLE for the header by calling CONSTBUFFER_CreateWithMoveMemory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_008: [ constbuffer_array_serializer_generate_header shall succeed and return the allocated CONSTBUFFER_HANDLE. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_3_buffers_works)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = generate_test_buffer_array_increasing_size(3, 10, 10); // size 10, 20, 30

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(sizeof(uint32_t) + sizeof(uint32_t) * 3));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 3));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 0));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 10));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 1));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 20));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 2));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 30));

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, sizeof(uint32_t) + sizeof(uint32_t) * 3));

    /// act
    CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

    /// assert
    ASSERT_IS_NOT_NULL(header);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    const CONSTBUFFER* header_data = real_CONSTBUFFER_GetContent(header);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t) + sizeof(uint32_t) * 3, header_data->size);

    uint32_t buffer_count;
    real_read_uint32_t(header_data->buffer, &buffer_count);
    ASSERT_ARE_EQUAL(uint32_t, 3, buffer_count);

    uint32_t buffer_size;
    real_read_uint32_t(header_data->buffer + sizeof(uint32_t), &buffer_size);
    ASSERT_ARE_EQUAL(uint32_t, 10, buffer_size);
    real_read_uint32_t(header_data->buffer + sizeof(uint32_t) + sizeof(uint32_t), &buffer_size);
    ASSERT_ARE_EQUAL(uint32_t, 20, buffer_size);
    real_read_uint32_t(header_data->buffer + sizeof(uint32_t) + 2 * sizeof(uint32_t), &buffer_size);
    ASSERT_ARE_EQUAL(uint32_t, 30, buffer_size);

    /// cleanup
    real_CONSTBUFFER_DecRef(header);
    real_constbuffer_array_dec_ref(data);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_42_007: [ If there are any other failures then constbuffer_array_serializer_generate_header shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serializer_generate_header_with_3_buffers_fails_when_underlying_functions_fail)
{
    /// arrange
    CONSTBUFFER_ARRAY_HANDLE data = generate_test_buffer_array_increasing_size(3, 10, 10); // size 10, 20, 30

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(data, IGNORED_ARG))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(malloc(sizeof(uint32_t) + sizeof(uint32_t) * 3));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 3));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 0))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 10));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 1))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 20));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(data, 2))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 30));

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, sizeof(uint32_t) + sizeof(uint32_t) * 3));

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            /// act
            CONSTBUFFER_HANDLE header = constbuffer_array_serializer_generate_header(data);

            ///assert
            ASSERT_IS_NULL(header, "On failed call %zu", i);
        }
    }

    /// cleanup
    real_constbuffer_array_dec_ref(data);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_001: [ If source is NULL then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_source_NULL_fails)
{
    ///arrange

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(NULL);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
}

static void constbuffer_array_serialize_with_1_buffer_inert_path(CONSTBUFFER_ARRAY_HANDLE source, uint32_t bufferSize)
{
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(source, IGNORED_ARG));

    uint32_t serializedSize = sizeof(uint32_t) + 1 * sizeof(uint32_t) + bufferSize;
    STRICT_EXPECTED_CALL(malloc(serializedSize));

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 1)); /*nBuffers is 1*/

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(source, 0))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, bufferSize));/*the only buffer has size "bufferSize"*/

    /*memcpy happens here*/

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, serializedSize));
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [ constbuffer_array_serialize shall get the number of CONSTBUFFER_HANDLEs inside source. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [ constbuffer_array_serialize shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [ constbuffer_array_serialize shall ensure that the total serialized size does not exceed UINT32_MAX. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [ constbuffer_array_serialize shall allocate enough memory to hold all the serialized bytes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [ constbuffer_array_serialize shall write at offset 0 the number of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [ constbuffer_array_serialize shall write at consecutive offsets the size of each buffer. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [ constbuffer_array_serialize shall write at consecutive offsets the content of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [ constbuffer_array_serialize shall call CONSTBUFFER_CreateWithMoveMemory from the allocated and written memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [ constbuffer_array_serialize shall succeed and return a non-zero value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1_buffer_succeeds)
{
    ///arrange
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(1, bufferSize, 0); /*one buffer of 2 bytes*/
    
    constbuffer_array_serialize_with_1_buffer_inert_path(source, bufferSize);

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    const CONSTBUFFER* content = real_CONSTBUFFER_GetContent(result);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t)+1*sizeof(uint32_t)+ bufferSize, content->size);
    ASSERT_ARE_EQUAL(uint8_t, 'a', (content->buffer+ sizeof(uint32_t) + 1 * sizeof(uint32_t))[0]);
    ASSERT_ARE_EQUAL(uint8_t, 'a', (content->buffer + sizeof(uint32_t) + 1 * sizeof(uint32_t))[1]);

    ///clean
    
    real_CONSTBUFFER_DecRef(result);
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1_buffer_unhappy_paths)
{
    ///arrange
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(1, bufferSize, 0); /*one buffer of 2 bytes*/

    constbuffer_array_serialize_with_1_buffer_inert_path(source, bufferSize);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

            ///assert
            ASSERT_IS_NULL(result, "On failed call %zu", i);
        }
    }

    ///clean
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [ constbuffer_array_serialize shall get the number of CONSTBUFFER_HANDLEs inside source. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [ constbuffer_array_serialize shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [ constbuffer_array_serialize shall ensure that the total serialized size does not exceed UINT32_MAX. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [ constbuffer_array_serialize shall allocate enough memory to hold all the serialized bytes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [ constbuffer_array_serialize shall write at offset 0 the number of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [ constbuffer_array_serialize shall write at consecutive offsets the size of each buffer. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [ constbuffer_array_serialize shall write at consecutive offsets the content of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [ constbuffer_array_serialize shall call CONSTBUFFER_CreateWithMoveMemory from the allocated and written memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [ constbuffer_array_serialize shall succeed and return a non-zero value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1_buffer_of_size_UINT32_MAX_minus_sizeof_uint32_t_minus_sizeof_uint32_t_succeeds) /*this test wants to see that a buffer that in its serialized form has exactly UINT32_MAX bytes - (physical_sector_size-1) is possible*/
{
    ///arrange
    uint32_t bufferSize = UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE - 1) - sizeof(uint32_t) - sizeof(uint32_t);
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(1, bufferSize, 0); /*one buffer of max size possible*/

    constbuffer_array_serialize_with_1_buffer_inert_path(source, bufferSize);

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(result);
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1_buffer_of_size_UINT32_MAX_minus_sizeof_uint32_t_minus_sizeof_uint32_t_plus_1_fails) /*this test wants to see that a buffer that in its serialized form has exactly UINT32_MAX bytes - (physical_sector_size-1)plus 1 is not possible*/
{
    ///arrange
    uint32_t bufferSize = UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE-1) - sizeof(uint32_t) - sizeof(uint32_t) + 1;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(1, bufferSize, 0); /*one too big buffer size*/

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG));

    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(source, IGNORED_ARG));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_constbuffer_array_dec_ref(source);
}


#if 0 /*test does not even begin to start... 1GB of items is huge*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1GB_of_buffers_succeeds) /*this test wants to see 1GB of buffers all of 0 size can be serialized*/
{
    ///arrange
    uint32_t nBuffers = 1;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(nBuffers, 0, 0); /*err... 1073741820 buffers of 0 bytes should still be serializable*/

    uint32_t nBuffersLIE = UINT32_MAX / sizeof(uint32_t) - 1 ;
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG))
        .CopyOutArgumentBuffer_buffer_count(&nBuffersLIE, sizeof(nBuffersLIE));

    uint32_t zero = 0;
    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(source, IGNORED_ARG))
        .CopyOutArgumentBuffer_all_buffers_size(&zero, sizeof(zero));

    uint32_t serializedSize = sizeof(uint32_t) + nBuffersLIE * sizeof(uint32_t) + 0 * nBuffersLIE;
    STRICT_EXPECTED_CALL(malloc(serializedSize));

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, nBuffersLIE));

    CONSTBUFFER constBufferLIE = {NULL, 0};
    const CONSTBUFFER* pconstBufferLIE = &constBufferLIE;

    for (uint32_t iBuffer = 0; iBuffer < nBuffersLIE; iBuffer++)
    {
        STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(source, iBuffer))
            .SetReturn(pconstBufferLIE);

        STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 0)); /*all buffers have 0 size*/

        /*memcpy happens here*/
    }

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, serializedSize));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    CONSTBUFFER_DecRef(result);
    real_constbuffer_array_dec_ref(source);
}
#endif

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_1GB_of_buffers_plus_1_fails) /*this test wants to see 1GB+1 of buffers all of 0 size cannot be serialized*/
{
    ///arrange
    uint32_t nBuffers = 1;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(nBuffers, 0, 0); /*err... 1073741820 buffers of 0 bytes should still be serializable*/

    /*lie about the number of buffers*/
    uint32_t nBuffersLIE = UINT32_MAX / sizeof(uint32_t);
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG))
        .CopyOutArgumentBuffer_buffer_count(&nBuffersLIE, sizeof(nBuffersLIE));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_constbuffer_array_dec_ref(source);
}

static void constbuffer_array_serialize_with_0_buffer_inert_path(CONSTBUFFER_ARRAY_HANDLE source)
{
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(source, IGNORED_ARG));

    uint32_t serializedSize = sizeof(uint32_t) + 0 * sizeof(uint32_t);
    STRICT_EXPECTED_CALL(malloc(serializedSize));

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 0)); /*nBuffers is 0*/

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, serializedSize));
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [ constbuffer_array_serialize shall get the number of CONSTBUFFER_HANDLEs inside source. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [ constbuffer_array_serialize shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [ constbuffer_array_serialize shall ensure that the total serialized size does not exceed UINT32_MAX. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [ constbuffer_array_serialize shall allocate enough memory to hold all the serialized bytes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [ constbuffer_array_serialize shall write at offset 0 the number of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [ constbuffer_array_serialize shall write at consecutive offsets the size of each buffer. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [ constbuffer_array_serialize shall write at consecutive offsets the content of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [ constbuffer_array_serialize shall call CONSTBUFFER_CreateWithMoveMemory from the allocated and written memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [ constbuffer_array_serialize shall succeed and return a non-zero value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_0_buffer_succeeds)
{
    ///arrange
    CONSTBUFFER_ARRAY_HANDLE source = real_constbuffer_array_create_empty(); 

    constbuffer_array_serialize_with_0_buffer_inert_path(source);

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    const CONSTBUFFER* content = real_CONSTBUFFER_GetContent(result);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t), content->size);
    ///clean

    real_CONSTBUFFER_DecRef(result);
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_0_buffer_unhappy_paths)
{
    ///arrange
    CONSTBUFFER_ARRAY_HANDLE source = real_constbuffer_array_create_empty();

    constbuffer_array_serialize_with_0_buffer_inert_path(source);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

            ///assert
            ASSERT_IS_NULL(result, "On failed call %zu", i);
        }
    }

    ///clean
    real_constbuffer_array_dec_ref(source);
}

static void constbuffer_array_serialize_with_2_buffer_inert_path(CONSTBUFFER_ARRAY_HANDLE source, uint32_t bufferSize1, uint32_t bufferSize2)
{
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(source, IGNORED_ARG))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(source, IGNORED_ARG));

    uint32_t serializedSize = sizeof(uint32_t) + 2 * sizeof(uint32_t) + bufferSize1 + bufferSize2;
    STRICT_EXPECTED_CALL(malloc(serializedSize));

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 2)); /*nBuffers is 2*/

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(source, 0))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, bufferSize1));

    /*memcpy happens here*/

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(source, 1))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, bufferSize2));

    /*memcpy happens here*/

    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, serializedSize));
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_002: [ constbuffer_array_serialize shall get the number of CONSTBUFFER_HANDLEs inside source. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_003: [ constbuffer_array_serialize shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_004: [ constbuffer_array_serialize shall ensure that the total serialized size does not exceed UINT32_MAX. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_026: [ constbuffer_array_serialize shall allocate enough memory to hold all the serialized bytes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_006: [ constbuffer_array_serialize shall write at offset 0 the number of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_027: [ constbuffer_array_serialize shall write at consecutive offsets the size of each buffer. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_008: [ constbuffer_array_serialize shall write at consecutive offsets the content of buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_009: [ constbuffer_array_serialize shall call CONSTBUFFER_CreateWithMoveMemory from the allocated and written memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_010: [ constbuffer_array_serialize shall succeed and return a non-zero value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_2_buffer_succeeds)
{
    ///arrange
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(2, bufferSize, 1); /*two buffer of 2 bytes and 3 bytes*/

    constbuffer_array_serialize_with_2_buffer_inert_path(source, bufferSize, bufferSize+1);

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    const CONSTBUFFER* content = real_CONSTBUFFER_GetContent(result);
    ASSERT_ARE_EQUAL(size_t, sizeof(uint32_t) + 2 * sizeof(uint32_t) + bufferSize + (bufferSize+1), content->size);
    const unsigned char* whereBufferContentsAre = content->buffer + sizeof(uint32_t) + 2 * sizeof(uint32_t);
    ASSERT_ARE_EQUAL(uint8_t, 'a', whereBufferContentsAre[0]);
    ASSERT_ARE_EQUAL(uint8_t, 'a', whereBufferContentsAre[1]);
    ASSERT_ARE_EQUAL(uint8_t, 'b', whereBufferContentsAre[2]);
    ASSERT_ARE_EQUAL(uint8_t, 'b', whereBufferContentsAre[3]);
    ASSERT_ARE_EQUAL(uint8_t, 'b', whereBufferContentsAre[4]);

    ///clean

    real_CONSTBUFFER_DecRef(result);
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_011: [ If there are any failures then constbuffer_array_serialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_2_buffer_unhappy_paths)
{
    ///arrange
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE source = generate_test_buffer_array_increasing_size(2, bufferSize, 1); /*two buffer of 2 bytes and 3 bytes*/

    constbuffer_array_serialize_with_2_buffer_inert_path(source, bufferSize, bufferSize+1);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_HANDLE result = constbuffer_array_serialize(source);

            ///assert
            ASSERT_IS_NULL(result, "On failed call %zu", i);
        }
    }

    ///clean
    real_constbuffer_array_dec_ref(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_012: [ If source is NULL then constbuffer_array_deserialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_with_source_NULL_fails)
{
    ///arrange

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(NULL);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
}

static void constbuffer_array_deserialize_from_empty_serialization_inert_path(CONSTBUFFER_HANDLE zero)
{
    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(zero))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG));

    STRICT_EXPECTED_CALL(constbuffer_array_create_empty());
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_013: [ constbuffer_array_deserialize shall get the buffer content by a call to CONSTBUFFER_GetContent. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_015: [ constbuffer_array_deserialize shall read a uint32_t from source that is the number of buffers (nBuffers). ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_016: [ If nBuffers is 0 then constbuffer_array_deserialize shall verify that source content does not contain other bytes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_017: [ If nBuffers is 0 then constbuffer_array_deserialize shall call constbuffer_array_create_empty, succeed, and return a non-NULL value. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_empty_serialization_succeeds)
{
    ///arrange
    uint32_t source = 0;
    CONSTBUFFER_HANDLE zero = real_CONSTBUFFER_Create((const unsigned char*)&source, sizeof(source));
    ASSERT_IS_NOT_NULL(zero);

    constbuffer_array_deserialize_from_empty_serialization_inert_path(zero);

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(zero);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_constbuffer_array_dec_ref(result);
    real_CONSTBUFFER_DecRef(zero);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_empty_serialization_unhappy_paths)
{
    ///arrange
    uint32_t source = 0;
    CONSTBUFFER_HANDLE zero = real_CONSTBUFFER_Create((const unsigned char*)&source, sizeof(source));
    ASSERT_IS_NOT_NULL(zero);

    constbuffer_array_deserialize_from_empty_serialization_inert_path(zero);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(zero);

            ///assert
            ASSERT_IS_NULL(result, "On failed call %zu", i);
        }
    }

    ///clean
    real_CONSTBUFFER_DecRef(zero);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_014: [ If the source size is smaller than sizeof(uint32_t) then constbuffer_array_deserialize shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_empty_serialization_from_not_enough_bytes_fails_1)
{
    ///arrange
    uint32_t source = 0;
    CONSTBUFFER_HANDLE zero = real_CONSTBUFFER_Create((const unsigned char*)&source, sizeof(source)-1);
    ASSERT_IS_NOT_NULL(zero);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(zero))
        .CallCannotFail();

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(zero);

    ///assert
    ASSERT_IS_NULL(result);

    ///clean
    real_CONSTBUFFER_DecRef(zero);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_016: [ If nBuffers is 0 then constbuffer_array_deserialize shall verify that source content does not contain other bytes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_empty_serialization_too_many_bytes_fails_1)
{
    ///arrange
    uint64_t source = 0;
    CONSTBUFFER_HANDLE zero = real_CONSTBUFFER_Create((const unsigned char*)&source, sizeof(uint32_t) + 1);
    ASSERT_IS_NOT_NULL(zero);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(zero))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG));

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(zero);

    ///assert
    ASSERT_IS_NULL(result);

    ///clean
    real_CONSTBUFFER_DecRef(zero);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_018: [ constbuffer_array_deserialize shall verify that source contains sufficient bytes to read nBuffers uint32_t values that are individual buffer sizes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_020: [ constbuffer_array_deserialize shall allocate memory to hold nBuffers CONSTBUFFER_HANDLEs. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_021: [ constbuffer_array_deserialize shall inc_ref source for every buffer that it constructs. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_029: [ constbuffer_array_deserialize shall construct nBuffers CONSTBUFFER_HANDLE by calls to CONSTBUFFER_CreateWithCustomFree with customFreeFunc parameter set to dec_ref_constbuffer ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_023: [ constbuffer_array_create shall call constbuffer_array_create passing the previously constructed CONSTBUFFER_HANDLE array. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_024: [ constbuffer_array_create shall succeed and return a non-NULL value. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_2_bytes_succeeds)
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2];
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t)+sizeof(uint32_t)+0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t)+1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/
    STRICT_EXPECTED_CALL(malloc(1 * sizeof(CONSTBUFFER_HANDLE))); /*there's just 1 constbuffer to build*/

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size again*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_IncRef(source));
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithCustomFree(IGNORED_ARG, 2, IGNORED_ARG, source));
    
    STRICT_EXPECTED_CALL(constbuffer_array_create(IGNORED_ARG, 1));

    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(IGNORED_ARG));
    STRICT_EXPECTED_CALL(free(IGNORED_ARG));

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    uint32_t nBuffers;
    ASSERT_ARE_EQUAL(int, 0, real_constbuffer_array_get_buffer_count(result, &nBuffers));
    ASSERT_ARE_EQUAL(uint32_t, 1, nBuffers);
    const CONSTBUFFER* buffer1 = real_constbuffer_array_get_buffer_content(result, 0);
    ASSERT_ARE_EQUAL(size_t, 2, buffer1->size);
    ASSERT_ARE_EQUAL(uint8_t, 'a', buffer1->buffer[0]);
    ASSERT_ARE_EQUAL(uint8_t, 'b', buffer1->buffer[1]);

    ///clean
    real_CONSTBUFFER_DecRef(source);
    real_constbuffer_array_dec_ref(result);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_2_bytes_with_too_many_bytes_fails)
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2 +1 /*+1 is extra*/];
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/ /*and gives up becauss there's too many bytes there*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_2_bytes_with_too_few_bytes_fails_1) /*in this case there are not enough bytes for the payload*/
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2];
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content) -1 /*this -1 makes it have too few bytes*/);
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/ /*and gives up becauss there's few many bytes there*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_2_bytes_with_too_few_bytes_fails_2) /*in this case there are not enough bytes for the size of buffer1*/
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2];
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(uint32_t)+sizeof(uint32_t)-1 /*-1 makes it impossible to read the size of the first buffer*/);
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_UINT32_MAX_minus_7_bytes_fails) /*-8 would perfectly be fine though*/
{
    ///arrange
    uint32_t bufferSize = UINT32_MAX - 7;
    uint32_t contentSize = sizeof(uint32_t) + sizeof(uint32_t) + 100;/*100 bytes of payload for buffer shoul;d never be reached to be read LIES*/
    unsigned char *content = (unsigned char *)my_gballoc_malloc(contentSize); 
    ASSERT_IS_NOT_NULL(content);
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), bufferSize);/*that has bufferSize bytes*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, contentSize);
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads bufferSize1 - and errors out*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);
    my_gballoc_free(content);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_UINT32_MAX_buffers_fails)
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2];
    real_write_uint32_t((unsigned char*)content + 0, UINT32_MAX/sizeof(uint32_t));/*this is too many buffers*/ /*LIES*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_1_buffer_of_UINT32_MAX_minus_8_bytes_succeeds) /*-8 would perfectly be fine though*/
{
    ///arrange
    uint32_t bufferSize = UINT32_MAX - 8;
    uint32_t contentSize = sizeof(uint32_t) + sizeof(uint32_t) + bufferSize;
    unsigned char *content = (unsigned char *)my_gballoc_malloc(contentSize);
    ASSERT_IS_NOT_NULL(content);
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), bufferSize);/*that has bufferSize bytes*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, contentSize);
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/
    STRICT_EXPECTED_CALL(malloc(1 * sizeof(CONSTBUFFER_HANDLE))); /*there's just 1 constbuffer to build*/

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size again*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_IncRef(source));
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithCustomFree(IGNORED_ARG, bufferSize, IGNORED_ARG, source));

    STRICT_EXPECTED_CALL(constbuffer_array_create(IGNORED_ARG, 1));

    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(IGNORED_ARG));
    STRICT_EXPECTED_CALL(free(IGNORED_ARG));

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);
    my_gballoc_free(content);
    real_constbuffer_array_dec_ref(result);

}

#if 0 /*test does not finish in any reasonable amount of time*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
TEST_FUNCTION(constbuffer_array_deserialize_from_UINT32_MAX_minus_1_buffers_succeds)
{
    ///arrange
    uint32_t nBuffers = UINT32_MAX / sizeof(uint32_t) - 1;
    unsigned char *content = (unsigned char *)my_gballoc_malloc(sizeof(uint32_t) + nBuffers * sizeof(uint32_t));
    ASSERT_IS_NOT_NULL(content);
    real_write_uint32_t((unsigned char*)content + 0, nBuffers);/*this is the absolute maximum number of buffers - they will all have size 0*/

    /*just say that the buffers all have size 0*/
    for (uint32_t iBuffer = 0; iBuffer < nBuffers; iBuffer++)
    {
        real_write_uint32_t((unsigned char*)content + sizeof(uint32_t)+ iBuffer*sizeof(uint32_t), 0);
    }

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(uint32_t) + nBuffers * sizeof(uint32_t));
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);
    real_constbuffer_array_dec_ref(result);

}
#endif


#define NBUFFERS_DEFINE 2
#define BUFFERSIZE1_DEFINE 3
#define BUFFERSIZE2_DEFINE 4

#define UNDECORATED_TEST_NAME MU_C2(MU_C4(constbuffer_array_deserialize_from_, NBUFFERS_DEFINE,_buffer_of_, BUFFERSIZE1_DEFINE), MU_C3(_and_, BUFFERSIZE2_DEFINE,_bytes_))

#define DECORATED_TEST_NAME_succeeds MU_C2(UNDECORATED_TEST_NAME, succeeds)
#define DECORATED_TEST_NAME_unhappy_paths MU_C2(UNDECORATED_TEST_NAME, unhappy_paths)

#define DECORATED_TEST_NAME_inert_path MU_C2(UNDECORATED_TEST_NAME, inert_path)

static void DECORATED_TEST_NAME_inert_path(CONSTBUFFER_HANDLE source)
{
    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer2 size*/

    STRICT_EXPECTED_CALL(malloc(2 * sizeof(CONSTBUFFER_HANDLE))); /*there's 2 constbuffers to build*/

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size again*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_IncRef(source));
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithCustomFree(IGNORED_ARG, BUFFERSIZE1_DEFINE, IGNORED_ARG, source));

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer2 size again*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_IncRef(source));
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithCustomFree(IGNORED_ARG, BUFFERSIZE2_DEFINE, IGNORED_ARG, source));

    STRICT_EXPECTED_CALL(constbuffer_array_create(IGNORED_ARG, NBUFFERS_DEFINE));

    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(IGNORED_ARG));
    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(IGNORED_ARG));
    STRICT_EXPECTED_CALL(free(IGNORED_ARG));
}


/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_018: [ constbuffer_array_deserialize shall verify that source contains sufficient bytes to read nBuffers uint32_t values that are individual buffer sizes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_019: [ constbuffer_array_deserialize shall verify that source contains exactly sufficient bytes to read nBuffers individual buffer sizes. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_020: [ constbuffer_array_deserialize shall allocate memory to hold nBuffers CONSTBUFFER_HANDLEs. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_021: [ constbuffer_array_deserialize shall inc_ref source for every buffer that it constructs. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_029: [ constbuffer_array_deserialize shall construct nBuffers CONSTBUFFER_HANDLE by calls to CONSTBUFFER_CreateWithCustomFree with customFreeFunc parameter set to dec_ref_constbuffer ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_023: [ constbuffer_array_create shall call constbuffer_array_create passing the previously constructed CONSTBUFFER_HANDLE array. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_024: [ constbuffer_array_create shall succeed and return a non-NULL value. ]*/
TEST_FUNCTION(DECORATED_TEST_NAME_succeeds)
{
    ///arrange
    uint32_t bufferSize1 = BUFFERSIZE1_DEFINE;
    uint32_t bufferSize2 = BUFFERSIZE2_DEFINE;
    uint32_t nBuffers = NBUFFERS_DEFINE;
    unsigned char content[sizeof(uint32_t) + NBUFFERS_DEFINE *sizeof(uint32_t) + BUFFERSIZE1_DEFINE + BUFFERSIZE2_DEFINE];
    real_write_uint32_t((unsigned char*)content + 0, nBuffers);/*there are 2 buffers*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), bufferSize1);/*that has 3 bytes*/
    real_write_uint32_t((unsigned char*)content + 2 * sizeof(uint32_t), bufferSize2);/*that has 4 bytes*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 0, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 1, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 2, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 3, 'b');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 4, 'b');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 5, 'b');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 6, 'b');/*'a'*/
    

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    DECORATED_TEST_NAME_inert_path(source);

    ///act
    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    uint32_t actualnBuffers;
    ASSERT_ARE_EQUAL(int, 0, real_constbuffer_array_get_buffer_count(result, &actualnBuffers));
    ASSERT_ARE_EQUAL(uint32_t, 2, nBuffers);
    const CONSTBUFFER* buffer1 = real_constbuffer_array_get_buffer_content(result, 0);
    ASSERT_ARE_EQUAL(size_t, BUFFERSIZE1_DEFINE, buffer1->size);
    for (uint32_t i = 0; i < BUFFERSIZE1_DEFINE; i++)
    {
        ASSERT_ARE_EQUAL(uint8_t, 'a', buffer1->buffer[i]);
    }

    const CONSTBUFFER* buffer2 = real_constbuffer_array_get_buffer_content(result, 1);
    ASSERT_ARE_EQUAL(size_t, BUFFERSIZE2_DEFINE, buffer2->size);
    for (uint32_t i = 0; i < BUFFERSIZE2_DEFINE; i++)
    {
        ASSERT_ARE_EQUAL(uint8_t, 'b', buffer2->buffer[i]);
    }
    

    ///clean
    real_CONSTBUFFER_DecRef(source);
    real_constbuffer_array_dec_ref(result);

}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_025: [ If there are any failures then constbuffer_array_create shall fail and return NULL. ]*/
TEST_FUNCTION(DECORATED_TEST_NAME_unhappy_paths)
{
    ///arrange
    uint32_t bufferSize1 = BUFFERSIZE1_DEFINE;
    uint32_t bufferSize2 = BUFFERSIZE2_DEFINE;
    uint32_t nBuffers = NBUFFERS_DEFINE;
    unsigned char content[sizeof(uint32_t) + NBUFFERS_DEFINE * sizeof(uint32_t) + BUFFERSIZE1_DEFINE + BUFFERSIZE2_DEFINE];
    real_write_uint32_t((unsigned char*)content + 0, nBuffers);/*there are 2 buffers*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), bufferSize1);/*that has 3 bytes*/
    real_write_uint32_t((unsigned char*)content + 2 * sizeof(uint32_t), bufferSize2);/*that has 4 bytes*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 0, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 1, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 2, 'a');/*'a'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 3, 'b');/*'b'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 4, 'b');/*'b'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 5, 'b');/*'b'*/
    real_write_uint8_t((unsigned char*)content + 3 * sizeof(uint32_t) + 6, 'b');/*'b'*/


    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    DECORATED_TEST_NAME_inert_path(source);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

            ///assert
            ASSERT_IS_NULL(result, "On failed call %zu", i);
        }
    }

    ///clean
    real_CONSTBUFFER_DecRef(source);

}

#undef DECORATED_TEST_NAME_unhappy_paths
#undef DECORATED_TEST_NAME_succeeds
#undef UNDECORATED_TEST_NAME
#undef NBUFFERS_DEFINE 
#undef BUFFERSIZE1_DEFINE
#undef BUFFERSIZE2_DEFINE

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_028: [ dec_ref_constbuffer shall call CONSTBUFFER_DecRef on context. ]*/
TEST_FUNCTION(dec_ref_constbuffer_is_called_when_disposing_1)
{
    ///arrange
    unsigned char content[sizeof(uint32_t) + sizeof(uint32_t) + 2];
    real_write_uint32_t((unsigned char*)content + 0, 1);/*there is 1 buffer*/
    real_write_uint32_t((unsigned char*)content + sizeof(uint32_t), 2);/*that has 2 bytes*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 0, 'a');/*first byte is 'a'*/
    real_write_uint8_t((unsigned char*)content + sizeof(uint32_t) + sizeof(uint32_t) + 1, 'b');/*second byte is 'b'*/

    CONSTBUFFER_HANDLE source = real_CONSTBUFFER_Create(content, sizeof(content));
    ASSERT_IS_NOT_NULL(source);

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(source))
        .CallCannotFail();

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads nbuffers*/
    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size*/
    STRICT_EXPECTED_CALL(malloc(1 * sizeof(CONSTBUFFER_HANDLE))); /*there's just 1 constbuffer to build*/

    STRICT_EXPECTED_CALL(read_uint32_t(IGNORED_ARG, IGNORED_ARG)); /*reads buffer1 size again*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_IncRef(source));
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithCustomFree(IGNORED_ARG, 2, IGNORED_ARG, source));

    STRICT_EXPECTED_CALL(constbuffer_array_create(IGNORED_ARG, 1));

    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(IGNORED_ARG));
    STRICT_EXPECTED_CALL(free(IGNORED_ARG));

    CONSTBUFFER_ARRAY_HANDLE result = constbuffer_array_deserialize(source);

    umock_c_reset_all_calls();

    STRICT_EXPECTED_CALL(CONSTBUFFER_DecRef(source));

    ///act
    real_constbuffer_array_dec_ref(result);

    ///assert
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///clean
    real_CONSTBUFFER_DecRef(source);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_030: [ If metadata is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_with_metadata_NULL_fails)
{
    ///arrange
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/
    ASSERT_IS_NOT_NULL(payload);
    uint32_t paddingBytes;

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(NULL, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_031: [ If payload is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_with_payload_NULL_fails)
{
    ///arrange
    unsigned char source[2];
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_Create(source, sizeof(source));
    ASSERT_IS_NOT_NULL(metadata);
    uint32_t paddingBytes;

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, NULL, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_047: [ If physical_sector_size is 0 then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_with_physical_sector_size_0_fails)
{
    ///arrange
    uint32_t metadataSize = UINT32_MAX - 10;/*-10 would still fit, as the next test proves*/
    unsigned char* metadataSource = (unsigned char*)my_gballoc_malloc(metadataSize);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(metadataSource, metadataSize);
    ASSERT_IS_NOT_NULL(metadata);

    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/ /*this has as serialized size 4+4+2 = 10 bytes*/
    ASSERT_IS_NOT_NULL(payload);

    uint32_t paddingBytes;

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, 0, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_048: [ If padding_bytes is NULL then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_with_padding_bytes_NULL_fails)
{
    ///arrange
    uint32_t metadataSize = 100;
    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(2, bufferSize, 1); /*two buffer of 2 bytes and 3 bytes*/
    ASSERT_IS_NOT_NULL(payload);

    unsigned char* metadataSource = (unsigned char*)my_gballoc_malloc(metadataSize);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(metadataSource, metadataSize);
    ASSERT_IS_NOT_NULL(metadata);

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, NULL);

    ///assert
    ASSERT_IS_NULL(result);

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_metadata_greater_or_equal_to_UINT32_MAX_fails)
{
    ///arrange
    unsigned char* source = (unsigned char*)my_gballoc_malloc((size_t)UINT32_MAX + 1);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(source, UINT32_MAX);
    ASSERT_IS_NOT_NULL(metadata);

    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/
    ASSERT_IS_NOT_NULL(payload);
    uint32_t paddingBytes;

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(metadata));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_metadata_greater_or_equal_to_UINT32_MAX_fails_2)
{
    ///arrange
    unsigned char* source = (unsigned char*)my_gballoc_malloc(UINT32_MAX - 1);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(source, UINT32_MAX);
    ASSERT_IS_NOT_NULL(metadata);

    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/
    ASSERT_IS_NOT_NULL(payload);
    uint32_t paddingBytes;

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(metadata));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_with_metadata_plus_payload_exceeding_size_fails)
{
    ///arrange
    uint32_t metadataSize = UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE_DEFINE - 1) - 10 + 1 ;/*-10 would still fit, as the next test proves*/
    unsigned char* metadataSource = (unsigned char*)my_gballoc_malloc(metadataSize);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(metadataSource, metadataSize);
    ASSERT_IS_NOT_NULL(metadata);

    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/ /*this has as serialized size 4+4+2 = 10 bytes*/
    ASSERT_IS_NOT_NULL(payload);
    uint32_t paddingBytes;

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(metadata));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(payload, IGNORED_ARG));
    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(payload, IGNORED_ARG));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_033: [ constbuffer_array_serialize_with_prepend shall get the number of CONSTBUFFER_HANDLEs inside payload. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_034: [ constbuffer_array_serialize_with_prepend shall get the size of all the buffers by a call to constbuffer_array_get_all_buffers_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_052: [ constbuffer_array_serialize_with_prepend shall ensure that the total serialized size does not exceed UINT32_MAX - (physical_sector_size-1). ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_050: [ constbuffer_array_serialize_with_prepend shall allocate memory to hold the complete serialization size that uses padding_bytes to reach a multiple of physical_sector_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_038: [ constbuffer_array_serialize_with_prepend shall copy the memory of metadata to the first bytes of the allocated memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_039: [ constbuffer_array_serialize_with_prepend shall write at the next offset the number of buffers in payload ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_040: [ constbuffer_array_serialize_with_prepend shall write at the next consecutive offsets the size of each buffer of payload ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_041: [ constbuffer_array_serialize_with_prepend shall write at consecutive offsets the content of payload's buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_042: [ constbuffer_array_serialize_with_prepend shall call CONSTBUFFER_CreateWithMoveMemory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_049: [ constbuffer_array_serialize_with_prepend shall succeed, write in padding_bytes the number of padding bytes it produced and return a non-NULL value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_succeeds_1) /*this has a VERY big metadata*/
{
    ///arrange
    uint32_t metadataSize = UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE_DEFINE-1) - 10 ;/*-10 still fits, as this test proves*/
    unsigned char* metadataSource = (unsigned char*)my_gballoc_malloc(metadataSize);
    CONSTBUFFER_HANDLE metadata = real_CONSTBUFFER_CreateWithMoveMemory(metadataSource, metadataSize);
    ASSERT_IS_NOT_NULL(metadata);

    uint32_t bufferSize = 2;
    CONSTBUFFER_ARRAY_HANDLE payload = generate_test_buffer_array_increasing_size(1, bufferSize, 1); /*one buffer of 2 bytes*/ /*this has as serialized size 4+4+2 = 10 bytes*/
    ASSERT_IS_NOT_NULL(payload);
    uint32_t paddingBytes;

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(metadata));
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(payload, IGNORED_ARG));
    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(payload, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE_DEFINE - 1)));
    /*here a memcpy happens from metadata to the buffer*/
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 1));
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(payload, 0));
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 2));
    /*here another memcpy happens from payload[0].buffer*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, UINT32_MAX - (TEST_PHYSICAL_SECTOR_SIZE_DEFINE - 1)));

    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
    real_CONSTBUFFER_DecRef(result);
}


static void constbuffer_array_serialize_with_prepend_2_buffers_succeeds_inert_path(uint32_t metadataSize, uint32_t bufferSize, CONSTBUFFER_HANDLE* metadata, CONSTBUFFER_ARRAY_HANDLE* payload, uint32_t* totalSerializedSize)
{
    *payload = generate_test_buffer_array_increasing_size(2, bufferSize, 1); /*two buffer of 2 bytes and 3 bytes*/
    ASSERT_IS_NOT_NULL(*payload);

    unsigned char* metadataSource = (unsigned char*)my_gballoc_malloc(metadataSize);
    *metadata = real_CONSTBUFFER_CreateWithMoveMemory(metadataSource, metadataSize);
    ASSERT_IS_NOT_NULL(*metadata);

    uint32_t usefulBytesNoPaddingBytes = metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t) + 2 + 3;
    *totalSerializedSize = (usefulBytesNoPaddingBytes -1)/TEST_PHYSICAL_SECTOR_SIZE*TEST_PHYSICAL_SECTOR_SIZE + TEST_PHYSICAL_SECTOR_SIZE;

    STRICT_EXPECTED_CALL(CONSTBUFFER_GetContent(*metadata))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_count(*payload, IGNORED_ARG))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(constbuffer_array_get_all_buffers_size(*payload, IGNORED_ARG));
    STRICT_EXPECTED_CALL(malloc(*totalSerializedSize));
    /*here a memcpy happens from metadata to the buffer*/
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 2));

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(*payload, 0))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 2));
    /*here another memcpy happens from payload[0].buffer*/

    STRICT_EXPECTED_CALL(constbuffer_array_get_buffer_content(*payload, 1))
        .CallCannotFail();
    STRICT_EXPECTED_CALL(write_uint32_t(IGNORED_ARG, 3));
    /*here another memcpy happens from payload[1].buffer*/
    STRICT_EXPECTED_CALL(CONSTBUFFER_CreateWithMoveMemory(IGNORED_ARG, *totalSerializedSize));
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_050: [ constbuffer_array_serialize_with_prepend shall allocate memory to hold the complete serialization size that uses padding_bytes to reach a multiple of physical_sector_size. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_038: [ constbuffer_array_serialize_with_prepend shall copy the memory of metadata to the first bytes of the allocated memory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_039: [ constbuffer_array_serialize_with_prepend shall write at the next offset the number of buffers in payload ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_040: [ constbuffer_array_serialize_with_prepend shall write at the next consecutive offsets the size of each buffer of payload ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_041: [ constbuffer_array_serialize_with_prepend shall write at consecutive offsets the content of payload's buffers. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_042: [ constbuffer_array_serialize_with_prepend shall call CONSTBUFFER_CreateWithMoveMemory. ]*/
/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_049: [ constbuffer_array_serialize_with_prepend shall succeed, write in padding_bytes the number of padding bytes it produced and return a non-NULL value. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_succeeds_2) /*this sees that TWO buffers can be serialized*/
{
    ///arrange
    uint32_t metadataSize = 100;
    CONSTBUFFER_HANDLE metadata;
    CONSTBUFFER_ARRAY_HANDLE payload;
    uint32_t bufferSize = 2;
    uint32_t totalSerializedSize;
    uint32_t paddingBytes;
    constbuffer_array_serialize_with_prepend_2_buffers_succeeds_inert_path(metadataSize, bufferSize, &metadata, &payload, &totalSerializedSize);
    
    ///act
    CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

    ///assert
    ASSERT_IS_NOT_NULL(result);
    ASSERT_ARE_EQUAL(char_ptr, umock_c_get_expected_calls(), umock_c_get_actual_calls());
    const CONSTBUFFER* resultContent = real_CONSTBUFFER_GetContent(result);
    ASSERT_ARE_EQUAL(size_t, totalSerializedSize, resultContent->size);

    const CONSTBUFFER* metadataContent = real_CONSTBUFFER_GetContent(metadata);
    ASSERT_IS_TRUE(0 == memcmp(metadataContent->buffer, resultContent->buffer, metadataSize));

    uint32_t temp;
    real_read_uint32_t(resultContent->buffer + metadataSize, &temp);
    ASSERT_ARE_EQUAL(uint32_t, 2, temp);/*number of buffers*/

    real_read_uint32_t(resultContent->buffer + metadataSize + sizeof(uint32_t), &temp);
    ASSERT_ARE_EQUAL(uint32_t, bufferSize, temp);/*sizeof first buffer is 2*/

    real_read_uint32_t(resultContent->buffer + metadataSize + sizeof(uint32_t) + sizeof(uint32_t), &temp);
    ASSERT_ARE_EQUAL(uint32_t, bufferSize + 1, temp);/*sizeof second buffer is 3*/

    ASSERT_ARE_EQUAL(uint8_t, 'a', (resultContent->buffer + metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t))[0]);/*buffer 1 has 'a' inside it*/
    ASSERT_ARE_EQUAL(uint8_t, 'a', (resultContent->buffer + metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t))[1]);/*buffer 1 has 'a' inside it*/
    ASSERT_ARE_EQUAL(uint8_t, 'b', (resultContent->buffer + metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t))[2]);/*buffer 1 has 'b' inside it*/
    ASSERT_ARE_EQUAL(uint8_t, 'b', (resultContent->buffer + metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t))[3]);/*buffer 1 has 'b' inside it*/
    ASSERT_ARE_EQUAL(uint8_t, 'b', (resultContent->buffer + metadataSize + sizeof(uint32_t) + 2 * sizeof(uint32_t))[4]);/*buffer 1 has 'b' inside it*/

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
    real_CONSTBUFFER_DecRef(result);
}

/*Tests_SRS_CONSTBUFFER_ARRAY_SERIALIZER_02_044: [ If there are any failures then constbuffer_array_serialize_with_prepend shall fail and return NULL. ]*/
TEST_FUNCTION(constbuffer_array_serialize_with_prepend_unhappy_paths)
{
    ///arrange
    uint32_t metadataSize = 100;
    CONSTBUFFER_HANDLE metadata;
    CONSTBUFFER_ARRAY_HANDLE payload;
    uint32_t bufferSize = 2;
    uint32_t totalSerializedSize;
    uint32_t paddingBytes;
    constbuffer_array_serialize_with_prepend_2_buffers_succeeds_inert_path(metadataSize, bufferSize, &metadata, &payload, &totalSerializedSize);

    umock_c_negative_tests_snapshot();

    for (size_t i = 0; i < umock_c_negative_tests_call_count(); i++)
    {
        if (umock_c_negative_tests_can_call_fail(i))
        {
            umock_c_negative_tests_reset();
            umock_c_negative_tests_fail_call(i);

            ///act
            CONSTBUFFER_HANDLE result = constbuffer_array_serialize_with_prepend(metadata, payload, TEST_PHYSICAL_SECTOR_SIZE, &paddingBytes);

            ASSERT_IS_NULL(result);
        }
    }

    ///cleanup
    real_CONSTBUFFER_DecRef(metadata);
    real_constbuffer_array_dec_ref(payload);
}
END_TEST_SUITE(constbuffer_array_serializer_unittests)
