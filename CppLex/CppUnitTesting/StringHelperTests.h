#pragma once
#include "..\..\..\Utils\std_string_helper.h"
#include <gtest/gtest.h>


using namespace std;



//starts_with test 0
TEST(StringHelperTests, StartsWith_0)
{
	auto result = starts_with("hello world!", "hell");
	ASSERT_EQ(true, result);
}

//starts_with test 1
TEST(StringHelperTests, StartsWith_1)
{
	auto result = starts_with("hello world!", "abc");
	ASSERT_EQ(false, result);
}

//starts_with test 2
TEST(StringHelperTests, StartsWith_2)
{
	auto result = starts_with("hello world!", "hello world bye!");
	ASSERT_EQ(false, result);
}

//starts_with test 3
TEST(StringHelperTests, StartsWith_3)
{
	auto result = starts_with("", "");
	ASSERT_EQ(true, result);
}

//ends_with test 0
TEST(StringHelperTests, EndsWith_0)
{
	auto result = ends_with("hello world!", "rld!");
	ASSERT_EQ(true, result);
}

//ends_with test 1
TEST(StringHelperTests, EndsWith_1)
{
	auto result = ends_with("hello world!", "abc");
	ASSERT_EQ(false, result);
}

//ends_with test 2
TEST(StringHelperTests, EndsWith_2)
{
	auto result = ends_with("hello world!", "hello world bye!");
	ASSERT_EQ(false, result);
}

//ends_with test 3
TEST(StringHelperTests, EndsWith_3)
{
	auto result = ends_with("", "");
	ASSERT_EQ(true, result);
}

