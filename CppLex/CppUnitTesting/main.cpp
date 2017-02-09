#include <stdio.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

//Tests
#include "StringHelperTests.h"
#include "CppLexTests.h"



int main(int argc, char* argv[])
{
	testing::InitGoogleTest(&argc, argv);
	auto result = RUN_ALL_TESTS();

	getchar();
	return result;
}