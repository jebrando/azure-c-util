// Copyright (c) Microsoft. All rights reserved.

#include <stddef.h>
#include "testrunnerswitcher.h"

int main(void)
{
    size_t failedTestCount = 0;
    RUN_TEST_SUITE(constbuffer_array_serializer_unittests, failedTestCount);
    return failedTestCount;
}