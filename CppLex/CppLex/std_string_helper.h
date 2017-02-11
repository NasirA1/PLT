#pragma once
#include <string>
#include <array>
#include <vector>
#include <sstream>
#include <codecvt>


using namespace std;


//Trim the given string from the left and return reference
//By default SPACE characters are trimmed. Custom characters can be provided in trim_chars parameter.
template<class CHARTYPE = char, size_t ARRSIZE = 1u>
inline static basic_string<CHARTYPE>& triml(basic_string<CHARTYPE>& s, const std::array<char, ARRSIZE>& trim_chars = { ' ' })
{
	for (const auto c : trim_chars)
		s.erase(0, s.find_first_not_of(c));
	return s;
}

//Trim the given string from the right and return reference
//By default SPACE characters are trimmed. Custom characters can be provided in trim_chars parameter.
template<class CHARTYPE = char, size_t ARRSIZE = 1u>
inline static basic_string<CHARTYPE>& trimr(basic_string<CHARTYPE>& s, const std::array<char, ARRSIZE>& trim_chars = { ' ' })
{
	for (const auto c : trim_chars)
		s.erase(s.find_last_not_of(c) + 1, s.size());
	return s;
}

//Trim the given string from both sides and return reference
//By default SPACE characters are trimmed. Custom characters can be provided in trim_chars parameter.
template<class CHARTYPE = char, size_t ARRSIZE = 1u>
inline static basic_string<CHARTYPE>& trim(basic_string<CHARTYPE>& s, const std::array<char, ARRSIZE>& trim_chars = { ' ' })
{
	return trimr(triml(s, trim_chars), trim_chars);
}


template<class CHARTYPE = char>
inline static bool starts_with(const basic_string<CHARTYPE>& subject, const basic_string<CHARTYPE>& prefix)
{
	if (prefix.length() > subject.length()) return false;
	return subject.substr(0, prefix.length()) == prefix;
}

template<class CHARTYPE = char>
inline static bool starts_with(const CHARTYPE* const subject, const CHARTYPE* const prefix)
{
	const basic_string<CHARTYPE> s_subject(subject);
	const basic_string<CHARTYPE> s_prefix(prefix);
	return starts_with(s_subject, s_prefix);
}

template<class CHARTYPE = char>
inline static bool starts_with(const basic_string<CHARTYPE>& subject, const CHARTYPE* const prefix)
{
	const basic_string<CHARTYPE> s_prefix(prefix);
	return starts_with(subject, s_prefix);
}


template<class CHARTYPE = char>
inline static bool ends_with(const basic_string<CHARTYPE>& subject, const basic_string<CHARTYPE>& suffix)
{
	if (suffix.length() > subject.length()) return false;
	return subject.substr(subject.length() - suffix.length(), suffix.length()) == suffix;
}

template<class CHARTYPE = char>
inline static bool ends_with(const CHARTYPE* const subject, const CHARTYPE* const suffix)
{
	const basic_string<CHARTYPE> s_subject(subject);
	const basic_string<CHARTYPE> s_suffix(suffix);
	return ends_with(s_subject, s_suffix);
}

template<class CHARTYPE = char>
inline static bool ends_with(const basic_string<CHARTYPE>& subject, const CHARTYPE* const suffix)
{
	const basic_string<CHARTYPE> s_suffix(suffix);
	return ends_with(subject, s_suffix);
}

template<class CHARTYPE = char>
inline static basic_string<CHARTYPE> copy(const basic_string<CHARTYPE>& s)
{
	return s;
}

template<class CHARTYPE = char>
inline static basic_string<CHARTYPE> copy(const CHARTYPE* const s)
{
	return s;
}

template<class CHARTYPE = char, size_t ARRSIZE = 1>
inline static auto split(const basic_string<CHARTYPE>& subject, const CHARTYPE delim = '\n', const bool ignore_empty = false)
{
	std::vector<std::basic_string<CHARTYPE>> output;
	std::basic_istringstream<CHARTYPE> iss(subject);

	for (std::basic_string<CHARTYPE> token; std::getline(iss, token, delim);)
	{
		if (ignore_empty && token.empty())
			continue;
		output.push_back(std::move(token));
	}

	return output;
}


static inline std::string wstring_to_string(const std::wstring& wstr)
{
	//use converter (.to_bytes: wstr->str, .from_bytes: str->wstr)
	using convert_type = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_type, wchar_t> converter;
	std::string astr = converter.to_bytes(wstr);
	return astr;
}
