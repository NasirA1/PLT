#include "CppLex.h"
#include <regex>
#include "..\..\..\Utils\std_string_helper.h"
#include <sstream>


using namespace std;

static const wstring WS = _T("[[:space:]]+");
static const wstring WS_OPT = _T("[[:space:]]*");
static const wstring CONST_OPT = _T("(const)*");
static const wstring IDENT = _T("&?") + CONST_OPT + WS_OPT + _T("[[:alnum:]_][[:alnum:]_\\:\\[\\]<>]*[&\\*]*");
static const wstring IDENT_OPT = _T("(") + IDENT + _T(")?");
static const wstring L_PAREN = _T("\\(");
static const wstring R_PAREN = _T("\\)");
static const wstring TERM = _T(";");
static const wstring SCOPE_START = _T("{");
static const wstring SCOPE_END = _T("}");
static const wstring SEPAR = _T(",");
static const wstring SEPAR_OPT = SEPAR + _T("?");
static const wstring PARAM = _T("(") + WS_OPT + IDENT + WS_OPT + IDENT_OPT + SEPAR_OPT + WS_OPT + _T("|void)");
static const wstring PARAM_LIST_OPT = _T("(") + PARAM + _T(")*");
static const wstring SPEC_OPT = WS_OPT + _T("(friend[[:space:]]+|static[[:space:]]+|inline[[:space:]]+|virtual[[:space:]]+)*");
static const wstring SUFF_OPT = _T("(const|final|override)?(const|final|override)?(const|final|override)?");
static const wstring PURE_VIRTUAL_OPT = _T("(") + WS_OPT + _T("=") + WS_OPT + _T("0") + _T(")*");
static const wstring COMMENT_LINE = _T("//");
static const wstring FUNC_HEAD = SPEC_OPT + IDENT + WS + IDENT + WS_OPT + L_PAREN + PARAM_LIST_OPT + R_PAREN + WS_OPT + CONST_OPT + PURE_VIRTUAL_OPT;



#if 0
bool IsFunctionDeclaration(const std::string& line)
{
	//Ignore comments
	if (starts_with(trim_tab_spaces(copy(line)), "//"))
		return false;

	smatch matches;
	string tail;
	const bool matched = regex_search(line, matches, reg_func);

	if (matched)
	{
		auto match = matches[0].str();
		tail = line.substr(matches.length(0), line.length() - matches.length(0));
	}

	return matched && tail == TERM;
}

bool IsFunctionDefinition(const std::string& line)
{
	//Ignore comments
	if (starts_with(trim_tab_spaces(copy(line)), "//"))
		return false;

	static const std::array<char, 4> trim_chars = { ' ', '\t', '\r', '\n' };
	smatch matches;
	string tail;
	const bool matched = regex_search(line, matches, reg_func);

	if (matched)
	{
		auto match = matches[0].str();
		tail = trim(line.substr(matches.length(0), line.length() - matches.length(0)), trim_chars);
	}

	return matched && (tail.empty() || tail.front() != ';');
}
#endif



#if 0
Maybe<ParseInfo> ParseLine(const std::string& line)
{
	smatch matches;
	ParseInfo info;
	string tail;
	bool matched = false;
	
	//Ignore comments
	if (starts_with(trim_tab_spaces(copy(line)), "//"))
		return info;

	//specifier
	matched = regex_search(line, matches, regex(SPEC_OPT));
	if (matched)
	{
		info.specifier = trim_tab_spaces(matches[0].str());
		tail = trim_tab_spaces(line.substr(matches.length(0), line.length() - matches.length(0)));
		
		//return type
		matched = regex_search(tail, matches, regex(IDENT));
		if (matched)
		{
			info.return_type = trim_tab_spaces(matches[0].str());
			tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));
			
			//name
			matched = regex_search(tail, matches, regex(IDENT));
			if (matched)
			{
				info.name = trim_tab_spaces(matches[0].str());
				tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

				//param list
				matched = regex_search(tail, matches, regex(L_PAREN + PARAM_LIST_OPT + R_PAREN));
				if (matched)
				{
					info.param_list = trim_tab_spaces(matches[0].str());
					tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

					//suffix
					matched = regex_search(tail, matches, regex(SUFF_OPT));
					while (matched && !matches[0].str().empty())
					{
						info.suffix += trim_tab_spaces(matches[0].str()) + ' ';
						tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));
						matched = regex_search(tail, matches, regex(SUFF_OPT));
					}
					trim_tab_spaces(info.suffix);
				}
			}
		}
	}

	return Just(info);
}
#endif



static const wregex reg_spec(SPEC_OPT, regex_constants::optimize);
static const wregex reg_ident(IDENT, regex_constants::optimize);
static const wregex reg_params(L_PAREN + PARAM_LIST_OPT + R_PAREN, regex_constants::optimize);
static const wregex reg_suffix(SUFF_OPT + PURE_VIRTUAL_OPT, regex_constants::optimize);
static const wregex reg_comm(_T("^([\\t ]+?\\/\\/.*)$"), regex_constants::optimize);
static const wregex reg_func_head(FUNC_HEAD, regex_constants::optimize);



std::pair<Maybe<ParseInfo>, std::wstring> ParseLine(const std::wstring& line)
{
	wsmatch matches;
	ParseInfo info;
	auto tail = line;
	bool matched = false;

	if (IsCommentLine(tail))
	{
		info.type = ParseInfo::PI_LINE_COMM;
		return std::make_pair(Just(info), L"");
	}


	const bool func_head_matched = regex_search(tail, reg_func_head);
	if (!func_head_matched)
	{
		info.type = ParseInfo::PI_UNKNOWN;
		return std::make_pair(Nothing<ParseInfo>(), L"");
	}


	trimr_all_whitespace(tail);
	//specifier
	matched = regex_search(tail, matches, reg_spec);
	if (matched)
	{
		info.specifier = trim_tab_spaces(matches[0].str());
		tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

		//return type
		matched = regex_search(tail, matches, reg_ident);
		if (matched)
		{
			info.return_type = trim_tab_spaces(matches[0].str());
			tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

			//name
			matched = regex_search(tail, matches, reg_ident);
			if (matched)
			{
				info.name = trim_tab_spaces(matches[0].str());
				tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

				//param list
				matched = regex_search(tail, matches, reg_params);
				if (matched)
				{
					info.param_list = trim_tab_spaces(matches[0].str());
					tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));

					//suffix
					matched = regex_search(tail, matches, reg_suffix);
					while (matched && !matches[0].str().empty())
					{
						info.suffix += trim_tab_spaces(matches[0].str()) + _T(' ');
						tail = trim_tab_spaces(tail.substr(matches.length(0), tail.length() - matches.length(0)));
						matched = regex_search(tail, matches, reg_suffix);
					}
					trim_tab_spaces(info.suffix);
				}
			}
		}
	}

	
	trim_all_whitespace(tail);

	if (func_head_matched)
	{
		if (starts_with(tail, TERM))
		{
			info.type = ParseInfo::PI_FUNC_DECL;
			tail = tail.substr(1, tail.length() - 1);
		}
		else
		{
			info.type = ParseInfo::PI_FUNC_DEFI;
			//tail = tail.substr(1, tail.length() - 1);
		}
	}
	else
	{
		info.type = ParseInfo::PI_UNKNOWN;
	}

	return std::make_pair(
		info.type == ParseInfo::PI_UNKNOWN? 
		Nothing<ParseInfo>(): 
		Just(info), 
	tail);
}



bool IsCommentLine(const std::wstring& line)
{
	return starts_with(trim_tab_spaces(copy(line)), _T("//"));
}


std::wstring ParseInfo::ToString() const
{
	wostringstream ss;

	if (!specifier.empty())
		ss << specifier << _T(' ');

	ss << return_type << _T(' ')
		<< name
		<< param_list;

	if (!suffix.empty())
		ss << _T(' ') << suffix;

	return ss.str();
}

