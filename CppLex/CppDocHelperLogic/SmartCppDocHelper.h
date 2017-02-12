#pragma once
#include <string>
#include <vector>
#include <set>
#include <map>

#ifndef UNIT_TESTS
#define FRIEND_TEST(a,b) //no-op
#endif



struct ISmartCppDocHelperView
{
	virtual ~ISmartCppDocHelperView() = default;
	virtual std::wstring GetSelectedFolder() = 0;
	virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;
	virtual void DisplayHeaderContent(const std::wstring& content, const bool enabledState) = 0;
	virtual void DisplaySourceContent(const std::wstring& content, const bool enabledState) = 0;
	virtual std::wstring GetHeaderContent() const = 0;
	virtual std::wstring GetSourceContent() const = 0;
	virtual void DisplayError(const std::wstring& message) = 0;
};


class SmartCppDocHelper
{
	FRIEND_TEST(SmartCppDocHelperTests, OnSelectProjectFolder);
	FRIEND_TEST(SmartCppDocHelperTests, OnSelectProjectFolder_Inaccessible);
	FRIEND_TEST(SmartCppDocHelperTests, OnSelectProjectItem_OneItem);
	FRIEND_TEST(SmartCppDocHelperTests, OnSelectProjectItem_MultipleItems);
	FRIEND_TEST(SmartCppDocHelperTests, OnCopyComments_OneFunction);
	FRIEND_TEST(SmartCppDocHelperTests, OnCopyComments_MultipleFunctions);

public:
	SmartCppDocHelper(ISmartCppDocHelperView& projectSelectionView);
	~SmartCppDocHelper();

	void OnSelectProjectFolder();
	void OnSelectProjectItem(const std::wstring& item);
	void OnCopyComments();
	void OnSave(const std::wstring& item);
	std::wstring GetSelectedProjectItem() const { return m_selectedProjectItem; }

private:
	void Clear();
	void PopulateProjectItems();


private:
	ISmartCppDocHelperView& m_View;
	std::wstring m_projectFolder;
	std::set<std::wstring> m_projectItems;
	std::wstring m_selectedProjectItem;
	std::map<std::wstring, std::wstring> m_HeaderFiles;
	std::map<std::wstring, std::wstring> m_SourceFiles;
};


