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
	virtual std::wstring SelectProject() const = 0;
	virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;
	virtual void DisplayHeaderContent(const std::wstring& content, const bool enabledState) = 0;
	virtual void DisplaySourceContent(const std::wstring& content, const bool enabledState) = 0;
	virtual ~ISmartCppDocHelperView() = default;
};


class SmartCppDocHelper
{
	FRIEND_TEST(SmartCppDocHelperTester, OnSelectProjectFolder);
	FRIEND_TEST(SmartCppDocHelperTester, OnSelectProjectItem);

public:
	SmartCppDocHelper(ISmartCppDocHelperView& projectSelectionView);
	~SmartCppDocHelper();

	void OnSelectProjectFolder();
	void OnSelectProjectItem(const std::wstring& item);

private:
	void Clear();
	void PopulateProjectItems();


private:
	ISmartCppDocHelperView& m_View;
	std::wstring m_projectFolder;
	std::set<std::wstring> m_projectItems;
	std::map<std::wstring, std::wstring> m_HeaderFiles;
	std::map<std::wstring, std::wstring> m_SourceFiles;
};


