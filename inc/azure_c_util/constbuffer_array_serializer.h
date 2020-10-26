// Copyright (C) Microsoft Corporation. All rights reserved.

#ifndef CONSTBUFFER_ARRAY_SERIALIZER_H
#define CONSTBUFFER_ARRAY_SERIALIZER_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#include "azure_macro_utils/macro_utils.h"

#include "azure_c_util/constbuffer_array.h"
#include "azure_c_util/constbuffer.h"

#include "umock_c/umock_c_prod.h"

#ifdef __cplusplus
extern "C" {
#endif

MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serializer_generate_header, CONSTBUFFER_ARRAY_HANDLE, data);

MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize, CONSTBUFFER_ARRAY_HANDLE, source);

MOCKABLE_FUNCTION(, CONSTBUFFER_ARRAY_HANDLE, constbuffer_array_deserialize, CONSTBUFFER_HANDLE, source);

MOCKABLE_FUNCTION(, CONSTBUFFER_HANDLE, constbuffer_array_serialize_with_prepend, CONSTBUFFER_HANDLE, metadata, CONSTBUFFER_ARRAY_HANDLE, payload, uint32_t, physical_sector_size, uint32_t*, padding_bytes);

#ifdef __cplusplus
}
#endif

#endif // CONSTBUFFER_ARRAY_SERIALIZER_H
