#Copyright (c) Microsoft. All rights reserved.
#Licensed under the MIT license. See LICENSE file in the project root for full license information.

cmake_minimum_required(VERSION 2.8.11)

set(theseTestsName thandle_ut)

set(${theseTestsName}_test_files
${theseTestsName}.c
)

set(${theseTestsName}_c_files
thandle_user.c
thandle_flex_user.c
)

set(${theseTestsName}_h_files
../../inc/azure_c_util/thandle.h
thandle_user.h
thandle_flex_user.h
)

build_test_artifacts(${theseTestsName} ON "tests/azure_c_util" ADDITIONAL_LIBS azure_c_pal azure_c_pal_reals)

