#include "stdafx.h"
#include "SmartCppDocHelper.h"
#include "CppLex.h"
#include <unordered_map>
#include<algorithm>


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
	m_projectFolder = m_View.GetSelectedFolder();

	if (!m_projectFolder.empty())
	{
		if (CanAccess(m_projectFolder, AccessModes::ReadWrite))
		{
			Clear();
			PopulateProjectItems();
			m_View.DisplayProjectItems(m_projectItems);
		}
		else
		{
			m_View.DisplayError(L"Unable to access project folder. Please ensure that it exists and is accessible.");
		}
	}
}



void SmartCppDocHelper::OnSelectProjectItem(const std::wstring& item)
{
#define DISPLAY_CONTENT(type)																				\
	{																																	\
		const auto type##Found = m_##type##Files.find(item);						\
		if (type##Found != m_##type##Files.end())												\
		{																																\
			m_selectedProjectItem = item;																	\
			const auto type##Text = readAllText(type##Found->second);			\
			m_View.Display##type##Content(type##Text, true);							\
		}																																\
		else																														\
		{																																\
			m_selectedProjectItem = L"";																	\
			m_View.Display##type##Content(L"Content unavailable", false);	\
		}																																\
	} //End of macro

	DISPLAY_CONTENT(Header);
	DISPLAY_CONTENT(Source);

#undef DISPLAY_CONTENT
}



void SmartCppDocHelper::OnCopyDoxyComments(const CopyDirection direction)
{
	TRACE(L"OnCopyDoxyComments...\n");
	scope_timer_t tm("OnCopyDoxyComments");

	//TODO clean up
	auto headerText = m_View.GetHeaderContent();
	auto sourceText = m_View.GetSourceContent();
	auto headerLines = split(headerText);
	auto sourceLines = split(sourceText);

	unordered_map<string, int> decls; //<funcname, lineno>
	unordered_map<string, int> defs;  //<funcname, lineno>
	unordered_map<string, wstring> decl_comms; //<funcname, comment>


	//Get declarations
	for (auto i = 0; i < static_cast<int>(headerLines.size()); ++i)
	{
		auto line = wstring_to_string(headerLines[i]);
		if (IsFunctionDeclaration(line))
			decls[GetFunctionInfo(line).name] = i;
	}

	//Get definitions
	for (auto i = 0; i < static_cast<int>(sourceLines.size()); ++i)
	{
		auto line = wstring_to_string(sourceLines[i]);
		if (IsFunctionDefinition(line))
		{
			auto parts = split(GetFunctionInfo(line).name, ':', true);
			if(parts.size() > 1)
				defs[parts[1]] = i;
		}
	}

	//Get comments
	for (const auto& dec : decls)
	{
		std::vector<int> comments_lines;
		for (auto j = dec.second - 1; j >= 0; --j)
		{
			if (IsCommentLine(wstring_to_string(headerLines[j])))
				comments_lines.push_back(j);
			else
				break;
		}
		if (comments_lines.size() > 0)
		{
			for (auto i = static_cast<int>(comments_lines.size()) - 1; i >= 0; --i)
				decl_comms[dec.first].append(L"\n" + trim_tab_spaces(headerLines[comments_lines[i]]));
		}
	}

	//go through the decls
	//if def with the same name is found, find comment and insert it on top of the def
	for (const auto& dec : decls)
	{
		auto& found_def = defs.find(dec.first);

		if (found_def != defs.end())
		{
			const auto& found_comms = decl_comms.find(dec.first);
			if (found_comms != decl_comms.end())
			{
				sourceLines[found_def->second - 1] = found_comms->second + sourceLines[found_def->second - 1];
			}
		}
	}


	const auto updatedSourceContent = join(sourceLines);
	m_View.DisplaySourceContent(updatedSourceContent, sourceLines.size() > 0);
}



void SmartCppDocHelper::OnSave(const std::wstring& item)
{
	//TODO
}


//TODO add test
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

