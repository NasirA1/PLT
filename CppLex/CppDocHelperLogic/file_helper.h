#pragma once
#include <string>
#include <fstream>
#include <afx.h>


template<class CHARTYPE = char>
std::basic_string<CHARTYPE> filename_from_path(const std::basic_string<CHARTYPE>& fullpath)
{
	auto name_pos = fullpath.find_last_of('\\') + 1;
	auto ext_pos = fullpath.find_last_of('.');
	auto just_the_name = fullpath.substr(name_pos, ext_pos - name_pos);
	return just_the_name;
}


//Reads all text from the given text file at once and returns result in a string
template<typename TChar = char>
std::basic_string<TChar> readAllText(const std::basic_string<TChar>& filename)
{
	std::basic_ifstream<TChar> file(filename);
	std::basic_string<TChar> str;

	file.seekg(0, std::ios::end);
	auto length = file.tellg();
	if (length <= 0) return str;

	str.reserve(static_cast<size_t>(length));
	file.seekg(0, std::ios::beg);
	str.assign((std::istreambuf_iterator<TChar>(file)), std::istreambuf_iterator<TChar>());

	return str;
}



template<typename Func>
void WalkDirectory(const LPCTSTR pstr, Func& action)
{
	CFileFind finder;

	// build a string with wildcards
	CString strWildcard(pstr);
	strWildcard += _T("\\*.*");

	// start working for files
	BOOL bWorking = finder.FindFile(strWildcard);

	while (bWorking)
	{
		bWorking = finder.FindNextFile();

		// skip . and .. files; otherwise, we'd
		// recur infinitely!
		if (finder.IsDots())
			continue;

		auto str = finder.GetFilePath();

		// if it's a directory, recursively search it
		if (finder.IsDirectory())
		{
			WalkDirectory(str, action);
		}
		else
		{
			action(str);
		}
	}

	finder.Close();
}
