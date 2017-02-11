#include "stdafx.h"
#include "SmartCppDocHelper.h"
#include "std_string_helper.h"
#include "file_helper.h"


SmartCppDocHelper::SmartCppDocHelper(ISmartCppDocHelperView& projectSelectionView)
	: m_View(projectSelectionView)
{
}


SmartCppDocHelper::~SmartCppDocHelper()
{
	PopulateProjectItems();
}


void SmartCppDocHelper::OnSelectProjectFolder()
{
	m_projectFolder = m_View.SelectProject();

	if (!m_projectFolder.empty())
	{
		Clear();
		PopulateProjectItems();
		m_View.DisplayProjectItems(m_projectItems);
	}
}


void SmartCppDocHelper::OnSelectProjectItem(const std::wstring& item)
{	
#define DISPLAY_CONTENT(type)																				\
	{																																	\
		const auto type##Found = m_##type##Files.find(item);						\
		if (type##Found != m_##type##Files.end())												\
		{																																\
			const auto type##Text = readAllText(type##Found->second);			\
			m_View.Display##type##Content(type##Text, true);							\
		}																																\
		else																														\
		{																																\
			m_View.Display##type##Content(L"Content unavailable", false);	\
		}																																\
	} //End of macro

	DISPLAY_CONTENT(Header);
	DISPLAY_CONTENT(Source);

#undef DISPLAY_CONTENT
}


void SmartCppDocHelper::Clear()
{
	m_projectItems.clear();
	m_HeaderFiles.clear();
	m_SourceFiles.clear();
}


void SmartCppDocHelper::PopulateProjectItems()
{	
	WalkDirectory(m_projectFolder.c_str(), [&](auto& s) 
	{
		const std::wstring filepath(s);
		const auto isHeader = ends_with(filepath, L".h");
		const auto isSource = ends_with(filepath, L".cpp");

		if (isHeader)
		{
			const auto friendlyName = filename_from_path(filepath);
			m_projectItems.insert(friendlyName);
			m_HeaderFiles[friendlyName] = filepath;
		}

		if (isSource)
		{
			const auto friendlyName = filename_from_path(filepath);
			m_projectItems.insert(friendlyName);
			m_SourceFiles[friendlyName] = filepath;
		}
	});
}

