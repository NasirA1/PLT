#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "SmartCppDocHelper.h"
#include "..\..\..\Utils\std_quick_files.h"
#include <algorithm>


using namespace std;
using namespace testing;



struct MockSmartCppDocHelperView : public ISmartCppDocHelperView
{
	MOCK_METHOD0(GetSelectedFolder, wstring());
	MOCK_METHOD1(DisplayProjectItems, void(const std::set<std::wstring>&));
	MOCK_METHOD2(DisplayHeaderContent, void(const std::wstring&, const bool enabledState));
	MOCK_METHOD2(DisplaySourceContent, void(const std::wstring&, const bool enabledState));
	MOCK_CONST_METHOD0(GetHeaderContent, wstring());
	MOCK_CONST_METHOD0(GetSourceContent, wstring());
	MOCK_METHOD1(DisplayError, void(const wstring&));
};



TEST(SmartCppDocHelperTests, OnSelectProjectFolder)
{
	const wstring TEST_PROJECT = L"TestData\\TestProject1\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select TestProject1
	//Item "Calculator" should be displayed in the Project Items list
	auto itemsToDisplay = std::set<std::wstring>{ L"Calculator" };
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(testing::Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));
	docHelper.OnSelectProjectFolder();
	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"Calculator"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);
}



TEST(SmartCppDocHelperTests, OnSelectProjectFolder_Inaccessible)
{
	const wstring TEST_PROJECT = L"TestData\\NonExistentFolder\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select an inaccessible folder
	//Error message should be displayed and projects items should be zero
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(testing::Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayError(wstring(L"Unable to access project folder. Please ensure that it exists and is accessible.")));
	docHelper.OnSelectProjectFolder();
	ASSERT_EQ(0, docHelper.m_projectItems.size());
}



TEST(SmartCppDocHelperTests, OnSelectProjectItem_OneItem)
{
	const wstring TEST_PROJECT = L"TestData\\TestProject1\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select TestProject1
	//Item "Calculator" should be displayed in the Project Items list
	auto itemsToDisplay = std::set<std::wstring>{ L"Calculator" };
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(testing::Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));
	docHelper.OnSelectProjectFolder();
	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"Calculator"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	//Select item "Calculator" from the Project Items list
	//Calculator.h contents should be displayed in the Header Content section
	//Calculator.cpp contents should be displayed in the Source Content section
	const auto originalHeaderContent = readAllText(docHelper.m_HeaderFiles[L"Calculator"]);
	const auto originalSourceContent = readAllText(docHelper.m_SourceFiles[L"Calculator"]);
	EXPECT_CALL(mockView, DisplayHeaderContent(originalHeaderContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(originalSourceContent, true));
	docHelper.OnSelectProjectItem(L"Calculator");
}



TEST(SmartCppDocHelperTests, OnSelectProjectItem_MultipleItems)
{
	const wstring TEST_PROJECT = L"TestData\\TestProject2\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select TestProject2
	//"itemsToDisplay" items should be displayed in the Project Items list
	auto itemsToDisplay = std::set<std::wstring>{ L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));
	docHelper.OnSelectProjectFolder();
	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	//Select item "MyPropertyGrid" from the Project Items list
	//MyPropertyGrid.h contents should be displayed in the Header Content section
	//MyPropertyGrid.cpp contents should be displayed in the Source Content section
	vector<wstring> projectItems;
	std::copy(itemsToDisplay.begin(), itemsToDisplay.end(), std::back_inserter(projectItems));

	//The first 3 items: "MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1"
	//have both header and cpp contents
	for (auto i = 0; i < 3; ++i)
	{
		const auto item = projectItems[i];
		auto headerContent = readAllText(docHelper.m_HeaderFiles[item]);
		auto sourceContent = readAllText(docHelper.m_SourceFiles[item]);
		EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
		EXPECT_CALL(mockView, DisplaySourceContent(sourceContent, true));
		docHelper.OnSelectProjectItem(item);
	}

	//Second half of the vector: L"resource", L"stdafx", L"targetver"
	//L"resource" and L"targetver" have header files but not cpp so the message L"Content unavailable"
	//must be displayed and the widget disabled
	for (auto i = 3; i < 6; ++i)
	{
		//skip L"stdafx" as it has a CPP file and we have already tested the behaviour above
		if (i == 4) { continue; }

		const auto item = projectItems[i];
		auto headerContent = readAllText(docHelper.m_HeaderFiles[item]);
		EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
		EXPECT_CALL(mockView, DisplaySourceContent(wstring(L"Content unavailable"), false));
		docHelper.OnSelectProjectItem(item);
	}
}



TEST(SmartCppDocHelperTests, OnCopyComments_OneFunction)
{
	const wstring TEST_PROJECT = L"TestData\\TestProject1\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select TestProject1
	//Item "Calculator" should be displayed in the Project Items list
	auto itemsToDisplay = std::set<std::wstring>{ L"Calculator"};
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(testing::Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));
	docHelper.OnSelectProjectFolder();
	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"Calculator"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	//Select item "Calculator" from the Project Items list
	//Calculator.h contents should be displayed in the Header Content section
	//Calculator.cpp contents should be displayed in the Source Content section
	const auto originalHeaderContent = readAllText(docHelper.m_HeaderFiles[L"Calculator"]);
	const auto originalSourceContent = readAllText(docHelper.m_SourceFiles[L"Calculator"]);
	EXPECT_CALL(mockView, DisplayHeaderContent(originalHeaderContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(originalSourceContent, true));
	docHelper.OnSelectProjectItem(L"Calculator");

	//Now click on "Copy Comments" command
	//Function Declaration comment in the header file should be copied over to the function definitions in source file
	const auto expectedSourceContent = readAllText(TEST_PROJECT + L"Expected_Calculator.cpp.txt");
	EXPECT_CALL(mockView, GetHeaderContent()).WillOnce(Return(originalHeaderContent));
	EXPECT_CALL(mockView, GetSourceContent()).WillOnce(Return(originalSourceContent));
	EXPECT_CALL(mockView, DisplaySourceContent(expectedSourceContent, true));
	docHelper.OnCopyComments();
}



TEST(SmartCppDocHelperTests, OnCopyComments_MultipleFunctions)
{
	const wstring TEST_PROJECT = L"TestData\\TestProject3\\";
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	//Select TestProject1
	//Item "Calculator" should be displayed in the Project Items list
	auto itemsToDisplay = std::set<std::wstring>{ L"Calculator" };
	EXPECT_CALL(mockView, GetSelectedFolder()).WillOnce(testing::Return(TEST_PROJECT));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));
	docHelper.OnSelectProjectFolder();
	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"Calculator"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	//Select item "Calculator" from the Project Items list
	//Calculator.h contents should be displayed in the Header Content section
	//Calculator.cpp contents should be displayed in the Source Content section
	const auto originalHeaderContent = readAllText(docHelper.m_HeaderFiles[L"Calculator"]);
	const auto originalSourceContent = readAllText(docHelper.m_SourceFiles[L"Calculator"]);
	EXPECT_CALL(mockView, DisplayHeaderContent(originalHeaderContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(originalSourceContent, true));
	docHelper.OnSelectProjectItem(L"Calculator");

	//Now click on "Copy Comments" command
	//All function declaration comments in the header file should be copied over to the function definitions in source file
	const auto expectedSourceContent = readAllText(TEST_PROJECT + L"Expected_Calculator.cpp.txt");
	EXPECT_CALL(mockView, GetHeaderContent()).WillOnce(Return(originalHeaderContent));
	EXPECT_CALL(mockView, GetSourceContent()).WillOnce(Return(originalSourceContent));
	EXPECT_CALL(mockView, DisplaySourceContent(expectedSourceContent, true));
	docHelper.OnCopyComments();
}

