#pragma once
#include <string>



//Returns true if the given line of code contains a C++ function declaration
bool IsFunctionDeclaration(const std::string& line);


//Returns true if the line of code contains a C++ function definition
bool IsFunctionDefinition(const std::string& line);


//Determines if the given line of code is a C++ "//" comment
bool IsCommentLine(const std::string& line);



//Information about a function
struct FunctionInfo
{
	FunctionInfo() {}

	FunctionInfo(const std::string& spec, const std::string& rt, const std::string& nm, const std::string& params, const std::string& suff) 
		: specifier(spec)
		, return_type(rt)
		, name(nm)
		, param_list(params)
		, suffix(suff)
	{}

	//Specifier, e.g. "virtual", "static", etc.
	std::string specifier;

	//Return type, e.g. "int", "char*", etc.
	std::string return_type;

	//Function name
	std::string name;

	//Parameter list (comma delimited)
	std::string param_list;

	//Suffix info (e.g. "const", "override", "final", etc)
	std::string suffix;

	//Back to string
	std::string ToString() const;
};


//Value equality of two FunctionInfo objects
static inline bool operator== (const FunctionInfo& left, const FunctionInfo& right)
{
	return left.specifier == right.specifier &&
		left.return_type == right.return_type &&
		left.name == right.name &&
		left.param_list == right.param_list &&
		left.suffix == right.suffix;
}


//Given a C++ function declaration or definition, returns function name or empty string if unsuccessful
FunctionInfo GetFunctionInfo(const std::string& line);



template<typename CharType = char>
static __forceinline std::basic_string<CharType>& trim_tab_spaces(std::basic_string<CharType>& s)
{
	static std::array<CharType, 2> trim_chars = { ' ', '\t' };
	return trim(s, trim_chars);
}

