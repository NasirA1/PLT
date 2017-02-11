#include "stdafx.h"
#include "SmartCppDocHelper.h"
#include "std_string_helper.h"
#include "file_helper.h"
#include "CppLex.h"
#include <codecvt>



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



void SmartCppDocHelper::OnCopyComments(const std::wstring& item)
{
	//TODO
	auto headerText = m_View.GetHeaderContent();
	auto sourceText = m_View.GetSourceContent();

	auto headerLines = split(headerText);
	auto sourceLines = split(sourceText);

	for (auto i = 0u; i < headerLines.size(); ++i)
	{
		//use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
		using convert_type = std::codecvt_utf8<wchar_t>;
		std::wstring_convert<convert_type, wchar_t> converter;
		std::string ascii_line = converter.to_bytes(headerLines[i]);

		if (IsFunctionDeclaration(ascii_line))
		{
			std::vector<wstring> comments;
			for (auto j = i - 1; j >= 0; --j)
			{
				if (IsCommentLine(converter.to_bytes(headerLines[i])))
				{
					comments.push_back(headerLines[i]);
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
					std::string ascii_line = converter.to_bytes(sourceLines[j]);
					if (IsFunctionDefinition(ascii_line))
					{
						auto def_info = GetFunctionInfo(ascii_line);

						if (decl_info.name == def_info.name) //TODO: weak. what about overloads?? improve this
						{
							//now insert the comments above the definition
							for (const auto& comment : comments)
								sourceLines.insert(sourceLines.begin() + j, comment);
							break;
						}
					}
				}
			}
		}
	}
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

