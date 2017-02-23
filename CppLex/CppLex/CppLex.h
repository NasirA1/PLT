#pragma once
#include <string>
#include <tchar.h>


template<class X> struct Maybe
{
	X value;
	const bool null;
	Maybe(X val) : value(std::move(val)), null(false) { }
	Maybe() : null(true) { }
};

template<class X> Maybe<X> Just(X x) { return Maybe<X>(std::move(x)); }

template<class X> Maybe<X> Nothing() { return Maybe<X>(); }


//Information about a function
struct ParseInfo
{
	enum Type
	{
		PI_UNKNOWN = -1,
		PI_FUNC_DECL,
		PI_FUNC_DEFI,
		PI_LINE_COMM
	};

	ParseInfo() : type(PI_UNKNOWN) {}

	ParseInfo(const std::wstring& spec, const std::wstring& rt, const std::wstring& nm, const std::wstring& params, const std::wstring& suff, const Type typ) 
		: specifier(spec)
		, return_type(rt)
		, name(nm)
		, param_list(params)
		, suffix(suff)
		, type(typ)
	{}

	//Specifier, e.g. "virtual", "static", etc.
	std::wstring specifier;

	//Return type, e.g. "int", "char*", etc.
	std::wstring return_type;

	//Function name
	std::wstring name;

	//Parameter list (comma delimited)
	std::wstring param_list;

	//Suffix info (e.g. "const", "override", "final", etc)
	std::wstring suffix;

	//Declaration/definition
	Type type;

	//Back to string
	std::wstring ToString() const;
};


//Value equality of two ParseInfo objects
static inline bool operator== (const ParseInfo& left, const ParseInfo& right)
{
	return left.specifier == right.specifier &&
		left.return_type == right.return_type &&
		left.name == right.name &&
		left.param_list == right.param_list &&
		left.suffix == right.suffix &&
		left.type == right.type;
}


//Given a C++ function declaration or definition, returns function name or empty string if unsuccessful
std::pair<Maybe<ParseInfo>, std::wstring> ParseLine(const std::wstring& line);



template<typename CharType = char>
static __forceinline std::basic_string<CharType>& trim_tab_spaces(std::basic_string<CharType>& s)
{
	static std::array<CharType, 2> trim_chars = { _T(' '), _T('\t') };
	return trim(s, trim_chars);
}


template<typename CharType = char>
static __forceinline std::basic_string<CharType>& trim_all_whitespace(std::basic_string<CharType>& s)
{
	static const std::array<CharType, 4> trim_chars = { _T(' '), _T('\t'), _T('\r'), _T('\n') };
	return trim(s, trim_chars);
}

template<typename CharType = char>
static __forceinline std::basic_string<CharType>& triml_all_whitespace(std::basic_string<CharType>& s)
{
	static const std::array<CharType, 4> trim_chars = { _T(' '), _T('\t'), _T('\r'), _T('\n') };
	return triml(s, trim_chars);
}

template<typename CharType = char>
static __forceinline std::basic_string<CharType>& trimr_all_whitespace(std::basic_string<CharType>& s)
{
	static const std::array<CharType, 4> trim_chars = { _T(' '), _T('\t'), _T('\r'), _T('\n') };
	return trimr(s, trim_chars);
}


//Determines if the given line of code is a C++ "//" comment
bool IsCommentLine(const std::wstring& line);

bool IsCommentLine_NoTrim(const std::wstring& line);
