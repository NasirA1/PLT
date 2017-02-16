#include "stdafx.h"
#include "SmartCppDocHelper.h"
#include "CppLex.h"
#include <unordered_map>
#include<algorithm>
#include<thread>
#include <future>


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
	//TODO
}

#if 0
unordered_map<string, int> GetFunctionDeclarations(const vector<wstring>& headerLines)
{
	scope_timer_t tm("Getting declarations");
	unordered_map<string, int> decls;

	for (auto i = 0; i < static_cast<int>(headerLines.size()); ++i)
	{
		auto line = wstring_to_string(headerLines[i]);
		if (IsFunctionDeclaration(line))
			decls[GetFunctionInfo(line).name] = i;
	}

	return decls;
}


unordered_map<string, int> GetFunctionDefinitions(const vector<wstring>& sourceLines)
{
	scope_timer_t tm("Getting definitions");
	unordered_map<string, int> defs;

	for (auto i = 0; i < static_cast<int>(sourceLines.size()); ++i)
	{
		auto line = wstring_to_string(sourceLines[i]);
		TRACE("processing %d [%s]\n", i, line.c_str());
		if (IsFunctionDefinition(line))
		{
			auto parts = split(GetFunctionInfo(line).name, ':', true);
			if (parts.size() > 1)
				defs[parts[1]] = i;
		}
	}

	return defs;
}

unordered_map<string, wstring> GetDeclarationComments(const vector<wstring>& headerLines, const unordered_map<string, int>& decls)
{
	scope_timer_t tm("Getting comments");
	unordered_map<string, wstring> decl_comms;

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
			{
				auto trimmed = trim_tab_spaces(copy(headerLines[comments_lines[i]]));
				decl_comms[dec.first].append(L"\n" + trimmed);
			}
		}
	}

	return decl_comms;
}


void SmartCppDocHelper::OnCopyDoxyComments(const CopyDirection direction)
{
	TRACE(L"OnCopyDoxyComments...\n");
	scope_timer_t tm("OnCopyDoxyComments");

	auto headerText = m_View.GetHeaderContent();
	auto sourceText = m_View.GetSourceContent();

	std::vector<wstring> headerLines, sourceLines;
	{
		scope_timer_t tm("Splitting into vectors");
		headerLines = split(headerText);
		sourceLines = split(sourceText);
	}


	auto f1 = std::async(GetFunctionDeclarations, headerLines);
	auto f2 = std::async(GetFunctionDefinitions, sourceLines);

	auto decls = f1.get(); 						//<funcname, lineno>
	auto defs = f2.get();							//<funcname, lineno>

	auto decl_comms = GetDeclarationComments(headerLines, decls); //<funcname, comment>


	//go through the decls
	//if def with the same name is found, find comment and insert it on top of the def
	{
		scope_timer_t tm("Copying comments");

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
	}

	wstring	updatedSourceContent;
	{
		scope_timer_t tm("Flattening vector back to string");
		updatedSourceContent = join(sourceLines);
	}
	m_View.DisplaySourceContent(updatedSourceContent, sourceLines.size() > 0);
}
#endif


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

