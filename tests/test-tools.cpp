// Copyright © 2026 chargebyte GmbH
// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <string>

extern "C" {
#include "tools.h"
}

namespace {

int CompareVersionSign(const char *lhs, const char *rhs)
{
    const int rc = compare_version(lhs, rhs);
    return (rc > 0) - (rc < 0);
}

struct VersionCase {
    const char *name;
    const char *lhs;
    const char *rhs;
    int expected_sign;
};

class CompareVersionTest : public ::testing::TestWithParam<VersionCase> {
};

TEST_P(CompareVersionTest, HandlesExpectedOrdering)
{
    const VersionCase test_case = GetParam();

    EXPECT_EQ(CompareVersionSign(test_case.lhs, test_case.rhs), test_case.expected_sign)
        << "compare_version(\"" << test_case.lhs << "\", \"" << test_case.rhs << "\")";
}

std::string VersionCaseName(const ::testing::TestParamInfo<VersionCase> &info)
{
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
    OriginalVersionTestCases,
    CompareVersionTest,
    ::testing::Values(
        VersionCase{"IdenticalSinglePart", "1", "1", 0},
        VersionCase{"TrailingZeroPartEqualsBareVersion", "1.0", "1", 0},
        VersionCase{"MultipleTrailingZeroPartsEqualBareVersion", "1.0.0", "1", 0},
        VersionCase{"MissingTrailingPartEqualsZeroPart", "1.2", "1.2.0", 0},
        VersionCase{"LeadingZeroInPartIgnored", "1.02", "1.2", 0},
        VersionCase{"LeadingZerosInAllPartsIgnored", "01.002", "1.2", 0},
        VersionCase{"LexicallyLongerNumericPartCanBeGreater", "1.10", "1.2", 1},
        VersionCase{"NumericallySmallerMajorVersionIsLess", "2.0", "10.0", -1},
        VersionCase{"LaterPatchVersionIsGreater", "3.0.1", "3.0.0", 1},
        VersionCase{"EarlierPatchVersionIsLess", "3.0.0", "3.0.1", -1},
        VersionCase{"ShorterNumericPartCanBeLess", "1.2.3", "1.2.30", -1},
        VersionCase{"LongerNumericPartCanBeGreater", "1.2.30", "1.2.3", 1},
        VersionCase{"LeadingZerosStillCompareNumerically", "1.045", "1.44", 1},
        VersionCase{"ExtraSuffixPartStillGreaterAfterNumericWin", "1.045.1", "1.44", 1},
        VersionCase{"EmptyVersionsAreEqual", "", "", 0},
        VersionCase{"ZeroEqualsEmptyVersion", "0", "", 0},
        VersionCase{"EmptyEqualsAllZeroVersion", "", "0.0.0", 0},
        VersionCase{"AllZeroStringEqualsZero", "000", "0", 0},
        VersionCase{"RepeatedSeparatorsAreIgnored", "1..2", "1.2", 0},
        VersionCase{"LeadingAndTrailingSeparatorsAreIgnored", ".1.2.", "1.2", 0}),
    VersionCaseName);

TEST(CompareVersionStandaloneTest, TreatsNonNumericSuffixAsGreaterThanBareVersion)
{
    EXPECT_GT(compare_version("1a", "1"), 0);
    EXPECT_LT(compare_version("1", "1a"), 0);
}

}  // namespace
