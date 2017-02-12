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



//Basic test
TEST(CppLexTests, IsFunctionDeclaration_0)
{
	string input = "void main (int argc, char* argv[]);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test no params
TEST(CppLexTests, IsFunctionDeclaration_1)
{
	string input = "void main();";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test no param names
TEST(CppLexTests, IsFunctionDeclaration_1b)
{
	string input = "int add(int,int);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test no params (b)
TEST(CppLexTests, IsFunctionDeclaration_1c)
{
	string input = "void main( void );";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test pointer params
TEST(CppLexTests, IsFunctionDeclaration_2)
{
	string input = "char* strstr(char* hackstack, char* needle);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test const parameters
TEST(CppLexTests, IsFunctionDeclaration_3)
{
	string input = "char* strstr(char* const hackstack, char* const needle);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test const return type
TEST(CppLexTests, IsFunctionDeclaration_4)
{
	string input = "const char* strstr(const char* const hackstack, const char* const needle);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test const return type (b)
TEST(CppLexTests, IsFunctionDeclaration_4b)
{
	string input = "const char* const strstr(const char* const hackstack, const char* const needle);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test references params
TEST(CppLexTests, IsFunctionDeclaration_5)
{
	//string input = "int min(const T& a, const T& b);";
	string input = "int min(int a, int b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test reference return types
TEST(CppLexTests, IsFunctionDeclaration_6)
{
	string input = "int& min(const T& a, const T& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test pointers to pointers
TEST(CppLexTests, IsFunctionDeclaration_7)
{
	string input = "int** foo(const int** a, const float** b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test universal references
TEST(CppLexTests, IsFunctionDeclaration_8)
{
	string input = "int&& foo(const int&& a, const float&& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Test pointer references
TEST(CppLexTests, IsFunctionDeclaration_9)
{
	string input = "int&* foo(const int*& a, const float**& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//friend functions
TEST(CppLexTests, IsFunctionDeclaration_10)
{
	string input = "friend void swap(const int& a, const int& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//static inline functions
TEST(CppLexTests, IsFunctionDeclaration_11)
{
	string input = "static inline void swap(const int& a, const int& b);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//static inline functions
TEST(CppLexTests, IsFunctionDeclaration_11b)
{
	string input = "inline friend static char* swap(blah a, blah y, blah zz);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//simple virtual function
TEST(CppLexTests, IsFunctionDeclaration_11bb)
{
	string input = "virtual void foo(int);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//virtual function with templated type argument
TEST(CppLexTests, IsFunctionDeclaration_11c)
{
	string input = "virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//pure virtual functions
TEST(CppLexTests, IsFunctionDeclaration_11d)
{
	string input = "virtual void DisplayProjectItems(const std::set<std::wstring>&) = 0;";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//pure virtual function with templated type argument
TEST(CppLexTests, IsFunctionDeclaration_11e)
{
	string input = "virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}

//namespaces
TEST(CppLexTests, IsFunctionDeclaration_12)
{
	string input = "std::string& trim(std::string& s);";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(true, result);
}


//Negative test 0 - class declaration
TEST(CppLexTests, NOT_FunctionDeclaration_0)
{
	string input = "class Foo";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(false, result);
}


//Negative test 1 - struct declaration
TEST(CppLexTests, NOT_FunctionDeclaration_1)
{
	string input = "struct Foo";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(false, result);
}


//Negative test 2 - function definition
TEST(CppLexTests, NOT_FunctionDeclaration_2)
{
	string input = "void main(int argc, char* argv[])\n{ return 0; }";
	auto result = IsFunctionDeclaration(input);
	ASSERT_EQ(false, result);
}


//Basic IsFunctionDefinition test
TEST(CppLexTests, IsFunctionDefinition_0)
{
	string input = "void main (int argc, char* argv[]) { }";
	auto result = IsFunctionDefinition(input);
	ASSERT_EQ(true, result);
}


//Basic IsFunctionDefinition test 2
TEST(CppLexTests, IsFunctionDefinition_1)
{
	string input = "int main(int argc, char* argv[]) \t\n{ return 0; }";
	auto result = IsFunctionDefinition(input);
	ASSERT_EQ(true, result);
}


//Negative test
TEST(CppLexTests, NOT_FunctionDefinition_0)
{
	string input = "int main(int argc, char* argv[]);";
	auto result = IsFunctionDefinition(input);
	ASSERT_EQ(false, result);
}


//GetFunctionInfo basic test
TEST(CppLexTests, GetFunctionInfo_0)
{
	string input = "int main(int argc, char* argv[]);";
	FunctionInfo expected("", "int", "main", "(int argc, char* argv[])", "");
	auto result = GetFunctionInfo(input);
	ASSERT_EQ(result, expected);
}


//GetFunctionInfo basic test
TEST(CppLexTests, GetFunctionInfo_1)
{
	string input = "virtual void to_string(const int value) { throw \"NotImplemented\"; }";
	FunctionInfo expected("virtual", "void", "to_string", "(const int value)", "");
	auto result = GetFunctionInfo(input);
	ASSERT_EQ(result, expected);
}


//GetFunctionInfo basic test
TEST(CppLexTests, GetFunctionInfo_2)
{
	string input = "static friend const char* get_version(int,object&,object&);";
	FunctionInfo expected("static friend", "const char*", "get_version", "(int,object&,object&)", "");
	auto result = GetFunctionInfo(input);
	ASSERT_EQ(result, expected);
}


//virtual function with templated type argument
TEST(CppLexTests, GetFunctionInfo_VirtualFunctions)
{
	string input = "virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems);";
	FunctionInfo expected("virtual", "void", "DisplayProjectItems", "(const std::set<std::wstring>& projectItems)", "");
	auto result = GetFunctionInfo(input);
	ASSERT_EQ(result, expected);
}


//pure virtual function with templated type argument
TEST(CppLexTests, GetFunctionInfo_PureVirtualFunctions)
{
	string input = "virtual void DisplayProjectItems(const std::set<std::wstring>& projectItems) = 0;";
	FunctionInfo expected("virtual", "void", "DisplayProjectItems", "(const std::set<std::wstring>& projectItems)", "");
	auto result = GetFunctionInfo(input);
	ASSERT_EQ(result, expected);
}


//IsCommentLine test 0
TEST(CppLexTests, IsCommentLine_0)
{
	string input = "			//This function adds two numbers and returns the result";
	auto result = IsCommentLine(input);
	ASSERT_EQ(true, result);
}


//IsCommentLine test 1
TEST(CppLexTests, IsCommentLine_1)
{
	string input = "void add(int a, int b) { //add here...\n}";
	auto result = IsCommentLine(input);
	ASSERT_EQ(false, result);
}


//IsCommentLine test 2
TEST(CppLexTests, IsCommentLine_2)
{
	string input = "";
	auto result = IsCommentLine(input);
	ASSERT_EQ(false, result);
}


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, IsFunctionDefinition_4)
{
	string input =
		"void CMainFrame::OnViewCustomize() \tconst\t\n"
		"{\n"
		"\tCMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* scan menus */);\n"
		"\tpDlgCust->EnableUserDefinedToolbars();\n"
		"\tpDlgCust->Create();\n"
		"}\n";

	auto result = IsFunctionDefinition(input);
	ASSERT_EQ(true, result);
}


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, GetFunctionInfo_3)
{
	string input =
		"void CMainFrame::OnViewCustomize() \tconst\t\n"
		"{\n"
		"\tCMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* scan menus */);\n"
		"\tpDlgCust->EnableUserDefinedToolbars();\n"
		"\tpDlgCust->Create();\n"
		"}\n";

	auto result = GetFunctionInfo(input);
	FunctionInfo expected("", "void", "CMainFrame::OnViewCustomize", "()", "const");
	ASSERT_EQ(expected, result);
}


//IsFunctionDefinition_4 Member functions
TEST(CppLexTests, Match_Declaration_With_Definition)
{
	string input =
		"class Foo\n"
		"{\n"
		"	//Bar takes an int, performs a random computation and discards the result\n"
		"	void Bar(int i);\n"
		"\n"
		"\n"
		"	//Returns the string representation of a Foo object\n"
		"	inline std::string ToString() const override { return \"\"; }\n"
		"\n"
		"\n"
		"};\n"
		"\n"
		"void Foo::Bar(int i) \n"
		"{\n"
		"	//TODO\n"
		"}\n";

	auto lines = split(input);
	map<int, FunctionInfo> func_decls;
	map<int, FunctionInfo> func_defs;
	vector<int> comment_lines;

	for (auto i = 0u; i < lines.size(); ++i)
	{
		if (IsFunctionDeclaration(lines[i]))
			func_decls.emplace(i, GetFunctionInfo(lines[i]));

		if (IsFunctionDefinition(lines[i]))
			func_defs.emplace(i, GetFunctionInfo(lines[i]));

		if (IsCommentLine(lines[i]))
			comment_lines.push_back(i);
	}


	cout << input << endl;
	for (const auto& info : func_decls)
	{
		cout << "Found function declaration at line#" << setw(3) << info.first << ":   \"" << info.second.ToString() << "\"" << endl;
	}

	for (const auto& info : func_defs)
	{
		cout << "Found function definition  at line#" << setw(3) << info.first << ":   \"" << info.second.ToString() << "\"" << endl;
	}

	cout << endl;
	for (const auto i : comment_lines)
	{
		cout << "Found comment at line# " << setw(2) << i << ":   \"" << lines[i] << "\"" << endl;
	}
	cout << endl;

	ASSERT_EQ(1, func_decls.size());					//found 1 declaration
	ASSERT_EQ(2, func_defs.size());						//found 2 definitions
	ASSERT_EQ(3, func_decls.begin()->first);	//declaration is at line 3
	ASSERT_EQ(7, func_defs.begin()->first);		//first definition is at line 5
	ASSERT_EQ(12, func_defs.rbegin()->first);	//second definition is at line 10

	auto func_parts = split(func_defs[12].name, ':', true);  //Split member function name using "::" to in order to get the function name
	auto func_def = func_parts[1];
	ASSERT_EQ(func_decls.begin()->second.name, func_def);			//Match the declaration function name with the definition function name
}

