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
	MOCK_CONST_METHOD0(GetHeaderContent, wstring());
	MOCK_CONST_METHOD0(GetSourceContent, wstring());
};



TEST(SmartCppDocHelperTester, OnSelectProjectFolder)
{
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	auto itemsToDisplay = std::set<std::wstring>{ L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
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

	auto itemsToDisplay = std::set<std::wstring>{ L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, SelectProject()).WillOnce(Return(TestProjectFolder));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));

	docHelper.OnSelectProjectFolder();

	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	auto headerContent = readAllText(docHelper.m_HeaderFiles[L"MyPropertyGrid"]);
	auto sourceContent = readAllText(docHelper.m_SourceFiles[L"MyPropertyGrid"]);;
	
	EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(sourceContent, true));
	docHelper.OnSelectProjectItem(L"MyPropertyGrid");
}



TEST(SmartCppDocHelperTester, OnCopyComments)
{
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	auto itemsToDisplay = std::set<std::wstring>{ L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, SelectProject()).WillOnce(testing::Return(TestProjectFolder));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));

	docHelper.OnSelectProjectFolder();

	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	auto headerContent = readAllText(docHelper.m_HeaderFiles[L"MyPropertyGrid"]);
	auto sourceContent = readAllText(docHelper.m_SourceFiles[L"MyPropertyGrid"]);;

	EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(sourceContent, true));
	docHelper.OnSelectProjectItem(L"MyPropertyGrid");

	EXPECT_CALL(mockView, GetHeaderContent()).WillOnce(Return(headerContent));
	EXPECT_CALL(mockView, GetSourceContent()).WillOnce(Return(sourceContent));
	docHelper.OnCopyComments(L"MyPropertyGrid");
}


#if 0 //TODO
TEST(SmartCppDocHelperTester, OnSave)
{
	MockSmartCppDocHelperView mockView;
	SmartCppDocHelper docHelper(mockView);

	auto itemsToDisplay = std::set<std::wstring>{ L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver" };
	EXPECT_CALL(mockView, SelectProject()).WillOnce(testing::Return(TestProjectFolder));
	EXPECT_CALL(mockView, DisplayProjectItems(itemsToDisplay));

	docHelper.OnSelectProjectFolder();

	ASSERT_THAT(docHelper.m_projectItems, ElementsAre(L"MyPropertyGrid", L"PropertyGridApp", L"PropertyPage1", L"resource", L"stdafx", L"targetver"));
	ASSERT_EQ(docHelper.m_projectItems, itemsToDisplay);

	auto headerContent = readAllText(docHelper.m_HeaderFiles[L"MyPropertyGrid"]);
	auto sourceContent = readAllText(docHelper.m_SourceFiles[L"MyPropertyGrid"]);;

	EXPECT_CALL(mockView, DisplayHeaderContent(headerContent, true));
	EXPECT_CALL(mockView, DisplaySourceContent(sourceContent, true));
	docHelper.OnSelectProjectItem(L"MyPropertyGrid");

	EXPECT_CALL(mockView, GetHeaderContent()).WillOnce(Return(headerContent));
	EXPECT_CALL(mockView, GetSourceContent()).WillOnce(Return(sourceContent));
	docHelper.OnCopyComments(L"MyPropertyGrid");


	docHelper.OnSave(L"MyPropertyGrid");
	ASSERT_TRUE(false); //Fail on purpose TODO: implement
}
#endif

