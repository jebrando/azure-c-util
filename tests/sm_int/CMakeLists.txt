#Copyright (c) Microsoft. All rights reserved.

set(theseTestsName sm_int)

set(${theseTestsName}_test_files
    ${theseTestsName}.c
)

set(${theseTestsName}_c_files
)

set(${theseTestsName}_cpp_files
)

set(${theseTestsName}_h_files
)

build_test_artifacts(${theseTestsName} ON "tests/azure_c_util" ADDITIONAL_LIBS azure_c_util azure_c_pal)
