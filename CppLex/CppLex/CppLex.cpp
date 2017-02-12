#include "CppLex.h"
#include <regex>
#include "..\..\..\Utils\std_string_helper.h"
#include <sstream>


using namespace std;

static const string WS = "[[:space:]]+";
static const string WS_OPT = "[[:space:]]*";
static const string CONST_OPT = "(const)*";
static const string IDENT = CONST_OPT + WS_OPT + "[[:alnum:]_][[:alnum:]_\\:\\[\\]<>]*[&\\*]*";
static const string IDENT_OPT = "(" + IDENT + ")?";
static const string L_PAREN = "\\(";
static const string R_PAREN = "\\)";
static const string TERM = ";";
static const string SEPAR = ",";
static const string SEPAR_OPT = SEPAR + "?";
static const string PARAM = "(" + WS_OPT + IDENT + WS_OPT + IDENT_OPT + SEPAR_OPT + WS_OPT + "|void)";
static const string PARAM_LIST = "(" + PARAM + ")*";
static const string SPEC = WS_OPT + "(friend[[:space:]]+|static[[:space:]]+|inline[[:space:]]+|virtual[[:space:]]+)*";
static const string SUFF_OPT = "(const|final|override)?(const|final|override)?(const|final|override)?";
static const string PURE_VIRTUAL_OPT = "(" + WS_OPT + "=" + WS_OPT + "0" + ")*";
static const string FUNC_HEAD = SPEC + IDENT + WS + IDENT + WS_OPT + L_PAREN + PARAM_LIST + R_PAREN + WS_OPT + CONST_OPT + PURE_VIRTUAL_OPT;



bool IsFunctionDeclaration(const std::string& line)
{
	smatch matches;
	string tail;
	const bool matched = regex_search(line, matches, regex(FUNC_HEAD));

	if (matched)
	{
		auto match = matches[0].str();
		tail = line.substr(matches.length(0), line.length() - matches.length(0));
	}

	return matched && tail == TERM;
}


bool IsFunctionDefinition(const std::string& line)
{
	static const std::array<char, 4> trim_chars = { ' ', '\t', '\r', '\n' };
	smatch matches;
	string tail;
	const bool matched = regex_search(line, matches, regex(FUNC_HEAD));

	if (matched)
	{
		auto match = matches[0].str();
		tail = trim(line.substr(matches.length(0), line.length() - matches.length(0)), trim_chars);
	}

	return matched && (tail.empty() || tail.front() != ';');
}


__forceinline std::string& trim_tab_spaces(std::string& s)
{
	static std::array<char, 2> trim_chars = { ' ', '\t' };
	return trim(s, trim_chars);
}


FunctionInfo GetFunctionInfo(const std::string& line)
{
	smatch matches;
	FunctionInfo info;
	string tail;
	bool matched = false;
	
	//specifier
	matched = regex_search(line, matches, regex(SPEC));
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
				matched = regex_search(tail, matches, regex(L_PAREN + PARAM_LIST + R_PAREN));
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

	return info;
}


bool IsCommentLine(const std::string& line)
{
	return starts_with(trim_tab_spaces(copy(line)), "//");
}


std::string FunctionInfo::ToString() const
{
	ostringstream ss;

	if (!specifier.empty())
		ss << specifier << ' ';

	ss << return_type << ' '
		<< name
		<< param_list;

	if (!suffix.empty())
		ss << ' ' << suffix;

	return ss.str();
}

