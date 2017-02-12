#include "stdafx.h"
#include "SmartCppDocHelper.h"
#include "CppLex.h"
#include "..\..\..\Utils\std_string_helper.h"
#include "..\..\..\Utils\std_quick_files.h"
#include "..\..\..\Utils\MFCUtil\MFCUtil\FileHelper.h"
#include <chrono>
#include <map>


SmartCppDocHelper::SmartCppDocHelper(ISmartCppDocHelperView& projectSelectionView)
	: m_View(projectSelectionView)
{
}



SmartCppDocHelper::~SmartCppDocHelper()
{
	PopulateProjectItems();
}



struct scope_timer
{
	scope_timer(const char* const label) 
		: label_(label)
		, start_(chrono::steady_clock::now()) 
	{}

	~scope_timer()
	{
		const auto end = chrono::steady_clock::now();
		const auto diff = end - start_;
		TRACE("%s took %f ms\n", label_, chrono::duration<double, milli>(diff).count());
	}
	
	const char* const label_;
	const chrono::time_point<chrono::steady_clock> start_;
};



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


static std::array<TCHAR, 2> trim_chars = { _T(' '), _T('\t') };
static std::array<char, 2> asc_trim_chars = { ' ', '\t' };

void SmartCppDocHelper::OnCopyDoxyComments()
{
	TRACE(L"OnCopyDoxyComments...\n");
	scope_timer tm("OnCopyDoxyComments");

	//TODO clean up
	auto headerText = m_View.GetHeaderContent();
	auto sourceText = m_View.GetSourceContent();
	auto headerLines = split(headerText);
	auto sourceLines = split(sourceText);
	set<wstring> done_lines; //Optimisation remember already done lines and skip them at later iterations

#if 1
	map<string, int> decls;
	map<string, int> defs;
	map<string, std::vector<std::wstring>> decl_comms;

	for (auto i = 0; i < static_cast<int>(headerLines.size()); ++i)
	{
		auto line = wstring_to_string(headerLines[i]);
		if (IsFunctionDeclaration(line))
			decls[GetFunctionInfo(line).name] = i;
	}

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


	for (const auto& dec : decls)
	{
		std::vector<wstring> comments;
		for (auto j = dec.second - 1; j >= 0; --j)
		{
			if (IsCommentLine(wstring_to_string(headerLines[j])))
			{
				comments.push_back(headerLines[j]);
				trim(comments.back(), trim_chars);
			}
			else
			{
				break;
			}
		}
		if (comments.size() > 0)
		{
			decl_comms[dec.first] = comments;
		}
	}

	for (const auto& dec : decls)
	{
		auto& found_def = defs.find(dec.first);

		if (found_def != defs.end())
		{
			const auto& found_comms = decl_comms.find(dec.first);
			if (found_comms != decl_comms.end())
			{
				for (const auto& comm : found_comms->second)
				{
					sourceLines.insert(sourceLines.begin() + found_def->second, comm);
				}

				if (found_comms->second.size() > 0)
				{
					for(auto& def : defs)
						if(def.second > found_def->second)
							def.second += found_comms->second.size();
				}
			}
		}
	}

#else
	for (auto i = 0; i < static_cast<int>(headerLines.size()); ++i)
	{
		//TRACE(L"Line # %d\n", i);
		std::string ascii_line = wstring_to_string(headerLines[i]);

		if (IsFunctionDeclaration(ascii_line))
		{
			std::vector<wstring> comments;
			for (auto j = i - 1; j >= 0; --j)
			{
				if (IsCommentLine(wstring_to_string(headerLines[j])))
				{
					comments.push_back(headerLines[j]);
					trim(comments.back(), trim_chars);
				}
				else
				{
					break;
				}
			}
			if (comments.size() > 0)
			{
				auto decl_info = GetFunctionInfo(ascii_line);
				
				for (auto j = 0u; j < sourceLines.size(); ++j)
				{
					if (done_lines.find(sourceLines[j]) == done_lines.end())
					{
						std::string ascii_line = wstring_to_string(sourceLines[j]);
						if (IsFunctionDefinition(ascii_line))
						{
							auto def_info = GetFunctionInfo(ascii_line);

							//Split member function name using "::" to in order to get the function name
							auto func_parts = split(def_info.name, ':', true);
							auto func_def = func_parts[1];

							if (decl_info.name == func_def) //TODO: weak. what about overloads?? improve this
							{
								done_lines.insert(sourceLines[j]);
								//now insert the comments above the definition
								for (const auto& comment : comments)
									sourceLines.insert(sourceLines.begin() + j, comment);
								comments.clear();
								break;
							}
						}
					}
				}
			}
		}
	}
#endif

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

