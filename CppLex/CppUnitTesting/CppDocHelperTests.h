#pragma once
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "SmartCppDocHelper.h"
#include <file_helper.h>


using namespace std;
using namespace testing;

const wstring TestProjectFolder = L"C:\\Users\\Nasir\\Documents\\Visual Studio 2015\\Projects\\MFC PropertyGrid Demo";


struct MockSmartCppDocHelperView : public ISmartCppDocHelperView
{
	MOCK_CONST_METHOD0(SelectProject, wstring());
	MOCK_METHOD1(DisplayProjectItems, void(const std::set<std::wstring>&));
	MOCK_METHOD2(DisplayHeaderContent, void(const std::wstring&, const bool enabledState));
	MOCK_METHOD2(DisplaySourceContent, void(const std::wstring&, const bool enabledState));
};



TEST(SmartCppDocHelperTester, OnSelectProjectFolder)
{
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	std::set<std::wstring> itemsToDisplay = { L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, SelectProject()).WillOnce(testing::Return(TestProjectFolder));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));

	docHelper.OnSelectProjectFolder();

	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);
}



TEST(SmartCppDocHelperTester, OnSelectProjectItem)
{
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	std::set<std::wstring> itemsToDisplay = { L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, SelectProject()).WillOnce(testing::Return(TestProjectFolder));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));

	docHelper.OnSelectProjectFolder();

	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	std::wstring headerContent = readAllText(docHelper.m_HeaderFiles[L"MyPropertyGrid"]);
	std::wstring sourceContent = readAllText(docHelper.m_SourceFiles[L"MyPropertyGrid"]);;
	
	EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(sourceContent, true));
	docHelper.OnSelectProjectItem(L"MyPropertyGrid");
}

