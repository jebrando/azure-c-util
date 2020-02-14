// Copyright (C) Microsoft Corporation. All rights reserved.

#ifndef CONSTBUFFER_ARRAY_BATCHER_NV_H
#define CONSTBUFFER_ARRAY_BATCHER_NV_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#include "azure_c_util/constbuffer_array.h"
#include "umock_c/umock_c_prod.h"

#ifdef __cplusplus
extern "C" {
#endif

MOCKABLE_FUNCTION(, CONSTBUFFER_ARRAY_HANDLE, constbuffer_array_batcher_nv_batch, CONSTBUFFER_ARRAY_HANDLE*, payloads, uint32_t, count);
MOCKABLE_FUNCTION(, CONSTBUFFER_ARRAY_HANDLE*, constbuffer_array_batcher_nv_unbatch, CONSTBUFFER_ARRAY_HANDLE, batch, uint32_t*, payload_count);

#ifdef __cplusplus
}
#endif

#endif /* CONSTBUFFER_ARRAY_BATCHER_NV_H */
