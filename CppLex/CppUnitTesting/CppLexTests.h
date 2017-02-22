#pragma once
#include "CppLex.h"
#include "..\..\..\Utils\std_string_helper.h"
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <iomanip>


using namespace std;


#if 0
//Test template functions
//No need for template functions because they are all define inline
TEST(CppLexTests, NotSupported_1)
{
	string input = "template<class T> T min(const T& a, const T& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//Basic test (b)
//Implicit return types not supported currently
//Ambiguous whether declaration or function-call
TEST(CppLexTests, NotSupported_0)
{
	string input = "foo();";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//Operator overloads - might do
TEST(CppLexTests, IsFunctionDeclaration_xx)
{
	string input = "ostream& operator<< (const ostream& out, const Foo& obj);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}
#endif



#if 1
//Basic test
TEST(CppLexTests, IsFunctionDeclaration_0)
{
	wstring input = _T("void main (int argc, char* argv[]);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test no params
TEST(CppLexTests, IsFunctionDeclaration_1)
{
	wstring input = _T("void main();");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test no param names
TEST(CppLexTests, IsFunctionDeclaration_1b)
{
	wstring input = _T("int add(int,int);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test no params (b)
TEST(CppLexTests, IsFunctionDeclaration_1c)
{
	wstring input = _T("void main( void );");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test pointer params
TEST(CppLexTests, IsFunctionDeclaration_2)
{
	wstring input = _T("char* strstr(char* hackstack, char* needle);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test const parameters
TEST(CppLexTests, IsFunctionDeclaration_3)
{
	wstring input = _T("char* strstr(char* const hackstack, char* const needle);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test const return type
TEST(CppLexTests, IsFunctionDeclaration_4)
{
	wstring input = _T("const char* strstr(const char* const hackstack, const char* const needle);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test const return type (b)
TEST(CppLexTests, IsFunctionDeclaration_4b)
{
	wstring input = _T("const char* const strstr(const char* const hackstack, const char* const needle);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test references params
TEST(CppLexTests, IsFunctionDeclaration_5)
{
	//string input = "int min(const T& a, const T& b);";
	wstring input = _T("int min(int a, int b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test reference return types
TEST(CppLexTests, IsFunctionDeclaration_6)
{
	wstring input = _T("int& min(const T& a, const T& b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test pointers to pointers
TEST(CppLexTests, IsFunctionDeclaration_7)
{
	wstring input = _T("int** foo(const int** a, const float** b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test universal references
TEST(CppLexTests, IsFunctionDeclaration_8)
{
	wstring input = _T("int&& foo(const int&& a, const float&& b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//Test pointer references
TEST(CppLexTests, IsFunctionDeclaration_9)
{
	wstring input = _T("int&* foo(const int*& a, const float**& b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//friend functions
TEST(CppLexTests, IsFunctionDeclaration_10)
{
	wstring input = _T("friend void swap(const int& a, const int& b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//static inline functions
TEST(CppLexTests, IsFunctionDeclaration_11)
{
	wstring input = _T("static inline void swap(const int& a, const int& b);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}


//static inline functions
TEST(CppLexTests, IsFunctionDeclaration_11b)
{
	wstring input = _T("inline friend static char* swap(blah a, blah y, blah zz);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}

//simple virtual function
TEST(CppLexTests, IsFunctionDeclaration_11bb)
{
	wstring input = _T("virtual void foo(int);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}

//virtual function with templated type argument
TEST(CppLexTests, IsFunctionDeclaration_11c)
{
	wstring input = _T("virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}

//pure virtual functions
TEST(CppLexTests, IsFunctionDeclaration_11d)
{
	wstring input = _T("virtual void DisplayProjectItems(const std::set<std::wstring>&) = 0;");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}

//pure virtual function with templated type argument
TEST(CppLexTests, IsFunctionDeclaration_11e)
{
	wstring input = _T("virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}

//namespaces
TEST(CppLexTests, IsFunctionDeclaration_12)
{
	wstring input = _T("std::string& trim(std::string& s);");
	auto result = ParseLine(input);
	ASSERT_TRUE(!result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DECL, result.first.value.type);
	ASSERT_TRUE(result.second.empty());
}



//Negative test 0 - class declaration
TEST(CppLexTests, NOT_FunctionDeclaration_0)
{
	wstring input = _T("class Foo");
	auto result = ParseLine(input);
	ASSERT_TRUE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_UNKNOWN, result.first.value.type);
}


//Negative test 1 - struct declaration
TEST(CppLexTests, NOT_FunctionDeclaration_1)
{
	wstring input = _T("struct Foo");
	auto result = ParseLine(input);
	ASSERT_TRUE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_UNKNOWN, result.first.value.type);
}


//Negative test 2 - function definition
TEST(CppLexTests, NOT_FunctionDeclaration_2)
{
	wstring input = _T("void main(int argc, char* argv[])\n{ return 0; }");
	auto result = ParseLine(input);
	ASSERT_TRUE(result.first.value.type != ParseInfo::PI_FUNC_DECL);
}


TEST(CppLexTests, NOT_FunctionDeclaration_3)
{
	wstring input = _T("   static	bool  isValid( const EftAccounts  &account )");
	auto result = ParseLine(input);
	ASSERT_TRUE(result.first.value.type != ParseInfo::PI_FUNC_DECL);
}


TEST(CppLexTests, NOT_FunctionDeclaration_4)
{
	wstring input = _T("      //|01.06.2012	Ritmars Zole #6506 Added if(selectedIndex < 0){selectedIndex = 0;} after selectedIndex = menu.Select();");
	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_LINE_COMM, result.first.value.type);
}


//Basic IsFunctionDefinition test
TEST(CppLexTests, IsFunctionDefinition_0)
{
	wstring input = _T("void main (int argc, char* argv[]) { }");
	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DEFI, result.first.value.type);
}



//Basic IsFunctionDefinition test 2
TEST(CppLexTests, IsFunctionDefinition_1)
{
	wstring input = _T("int main(int argc, char* argv[]) \t\n{ return 0; }");
	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DEFI, result.first.value.type);
}


//Negative test
TEST(CppLexTests, NOT_FunctionDefinition_0)
{
	wstring input = _T("int main(int argc, char* argv[]);");
	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_TRUE(result.first.value.type != ParseInfo::PI_FUNC_DEFI);
}


#if 0
//TODO
//Negative test
TEST(CppLexTests, NOT_FunctionDefinition_1)
{
	wstring input = _T("      XMLNode  accountsXml ( XMLNode::openFileHelper( ) );");
	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_TRUE(result.first.value.type != ParseInfo::PI_FUNC_DEFI);
}
#endif 



//ParseLine basic test
TEST(CppLexTests, GetFunctionInfo_0)
{
	wstring input = _T("int main(int argc, char* argv[]);");
	ParseInfo expected(_T(""), _T("int"), _T("main"), _T("(int argc, char* argv[])"), _T(""), ParseInfo::PI_FUNC_DECL);
	auto result = ParseLine(input);
	ASSERT_EQ(result.first.value, expected);
}


//ParseLine basic test
TEST(CppLexTests, GetFunctionInfo_1)
{
	wstring input = _T("virtual void to_string(const int value) { throw \"NotImplemented\"; }");
	ParseInfo expected(_T("virtual"), _T("void"), _T("to_string"), _T("(const int value)"), _T(""), ParseInfo::PI_FUNC_DEFI);
	auto result = ParseLine(input);
	ASSERT_EQ(result.first.value, expected);
}


//ParseLine basic test
TEST(CppLexTests, GetFunctionInfo_2)
{
	wstring input = _T("static friend const char* get_version(int,object&,object&);");
	ParseInfo expected(_T("static friend"), _T("const char*"), _T("get_version"), _T("(int,object&,object&)"), _T(""), ParseInfo::PI_FUNC_DECL);
	auto result = ParseLine(input);
	ASSERT_EQ(result.first.value, expected);
}


//virtual function with templated type argument
TEST(CppLexTests, GetFunctionInfo_VirtualFunctions)
{
	wstring input = _T("virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems);");
	ParseInfo expected(_T("virtual"), _T("void"), _T("DisplayProjectItems"), _T("(const std::set<std::wstring>& projectItems)"), _T(""), ParseInfo::PI_FUNC_DECL);
	auto result = ParseLine(input);
	ASSERT_EQ(result.first.value, expected);
}


//pure virtual function with templated type argument
TEST(CppLexTests, GetFunctionInfo_PureVirtualFunctions)
{
	wstring input = _T("virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;");
	ParseInfo expected(_T("virtual"), _T("void"), _T("DisplayProjectItems"), _T("(const std::set<std::wstring>& projectItems)"), _T("= 0"), ParseInfo::PI_FUNC_DECL);
	auto result = ParseLine(input);
	ASSERT_EQ(result.first.value, expected);
}


//IsCommentLine test 0
TEST(CppLexTests, IsCommentLine_0)
{
	wstring input = _T("			//This function adds two numbers and returns the result");
	auto result = IsCommentLine(input);
	ASSERT_EQ(true, result);
}


//IsCommentLine test 1
TEST(CppLexTests, IsCommentLine_1)
{
	wstring input = _T("void add(int a, int b) { //add here...\n}");
	auto result = IsCommentLine(input);
	ASSERT_EQ(false, result);
}


//IsCommentLine test 2
TEST(CppLexTests, IsCommentLine_2)
{
	wstring input = _T("");
	auto result = IsCommentLine(input);
	ASSERT_EQ(false, result);
}


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, IsFunctionDefinition_4)
{
	wstring input =
		_T("void CMainFrame::OnViewCustomize() \tconst\t\n")
		_T("{\n")
		_T("\tCMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* scan menus */);\n")
		_T("\tpDlgCust->EnableUserDefinedToolbars();\n")
		_T("\tpDlgCust->Create();\n")
		_T("}\n");

	auto result = ParseLine(input);
	ASSERT_FALSE(result.first.null);
	ASSERT_EQ(ParseInfo::PI_FUNC_DEFI, result.first.value.type);
}


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, GetFunctionInfo_3)
{
	wstring input =
		_T("void CMainFrame::OnViewCustomize() \tconst\t\n")
		_T("{\n")
		_T("\tCMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* scan menus */);\n")
		_T("\tpDlgCust->EnableUserDefinedToolbars();\n")
		_T("\tpDlgCust->Create();\n")
		_T("}\n");

	auto result = ParseLine(input);
	ParseInfo expected(_T(""), _T("void"), _T("CMainFrame::OnViewCustomize"), _T("()"), _T("const"), ParseInfo::PI_FUNC_DEFI);
	ASSERT_EQ(expected, result.first.value);
}
#endif


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, Match_Declaration_With_Definition)
{
	wstring input =
		_T("class Foo\n")
		_T("{\n")
		_T("	//Bar takes an int, performs a random computation and discards the result\n")
		_T("	void Bar(int i);\n")
		_T("\n")
		_T("\n")
		_T("	//Returns the string representation of a Foo object\n")
		_T("	inline std::string ToString() const override { return \"\"; }\n")
		_T("\n")
		_T("\n")
		_T("};\n")
		_T("\n")
		_T("void Foo::Bar(int i) \n")
		_T("{\n")
		_T("	//TODO\n")
		_T("}\n");

	map<int, ParseInfo> func_decls;
	map<int, ParseInfo> func_defs;
	vector<int> comment_lines;

	auto lines = split(input);

	for(auto i = 0u; i < lines.size(); ++i)
	{
		auto info = ParseLine(lines[i]);

		if (info.first.value.type == ParseInfo::PI_FUNC_DECL)
			func_decls.emplace(i, info.first.value);

		else if (info.first.value.type == ParseInfo::PI_FUNC_DEFI)
			func_defs.emplace(i, info.first.value);

		else if (info.first.value.type == ParseInfo::PI_LINE_COMM)
			comment_lines.push_back(i);
	}


	std::wcout << input << endl;
	for (const auto& info : func_decls)
	{
		std::wcout << _T("Found function declaration at line#") << setw(3) << info.first << _T(":   \"") << info.second.ToString() << _T("\"") << endl;
	}

	for (const auto& info : func_defs)
	{
		std::wcout << _T("Found function definition  at line#") << setw(3) << info.first << _T(":   \"") << info.second.ToString() << _T("\"") << endl;
	}

	std::wcout << endl;
	for (const auto i : comment_lines)
	{
		std::wcout << _T("Found comment at line# ") << setw(2) << i << _T(":   \"") << lines[i] << _T("\"") << endl;
	}
	std::wcout << endl;

	ASSERT_EQ(1, func_decls.size());					//found 1 declaration
	ASSERT_EQ(2, func_defs.size());						//found 2 definitions
	ASSERT_EQ(3, func_decls.begin()->first);	//declaration is at line 3
	ASSERT_EQ(7, func_defs.begin()->first);		//first definition is at line 5
	ASSERT_EQ(12, func_defs.rbegin()->first);	//second definition is at line 10

	auto func_parts = split(func_defs[12].name, _T(':'), true);  //Split member function name using "::" to in order to get the function name
	auto func_def = func_parts[1];
	ASSERT_EQ(func_decls.begin()->second.name, func_def);			//Match the declaration function name with the definition function name
}

