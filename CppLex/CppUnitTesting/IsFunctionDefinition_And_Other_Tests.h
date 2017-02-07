#pragma once
#include "TestHarness.h"
#include "CppLex.h"
#include "std_string_helper.h"
#include <map>
#include <iostream>
#include <iomanip>


using namespace std;


//Basic IsFunctionDefinition test
TEST(IsFunctionDefinition_0, MyUnitTests)
{
	string input = "void main (int argc, char* argv[]) { }";
	auto result = IsFunctionDefinition(input);
	CHECK_EQUAL(true, result);
}

//Basic IsFunctionDefinition test 2
TEST(IsFunctionDefinition_1, MyUnitTests)
{
	string input = "int main(int argc, char* argv[]) \t\n{ return 0; }";
	auto result = IsFunctionDefinition(input);
	CHECK_EQUAL(true, result);
}

//Negative test
TEST(NOT_FunctionDefinition_0, MyUnitTests)
{
	string input = "int main(int argc, char* argv[]);";
	auto result = IsFunctionDefinition(input);
	CHECK_EQUAL(false, result);
}

//GetFunctionInfo basic test
TEST(GetFunctionInfo_0, MyUnitTests)
{
	string input = "int main(int argc, char* argv[]);";
	FunctionInfo expected("", "int", "main", "(int argc, char* argv[])", "");
	auto result = GetFunctionInfo(input);
	CHECK(result == expected);
}

//GetFunctionInfo basic test
TEST(GetFunctionInfo_1, MyUnitTests)
{
	string input = "virtual void to_string(const int value) { throw \"NotImplemented\"; }";
	FunctionInfo expected("virtual", "void", "to_string", "(const int value)", "");
	auto result = GetFunctionInfo(input);
	CHECK(result == expected);
}

//GetFunctionInfo basic test
TEST(GetFunctionInfo_2, MyUnitTests)
{
	string input = "static friend const char* get_version(int,object&,object&);";
	FunctionInfo expected("static friend", "const char*", "get_version", "(int,object&,object&)", "");
	auto result = GetFunctionInfo(input);
	CHECK(result == expected);
}

//starts_with test 0
TEST(StartsWith_0, MyUnitTests)
{
	auto result = starts_with("hello world!", "hell");
	CHECK_EQUAL(true, result);
}

//starts_with test 1
TEST(StartsWith_1, MyUnitTests)
{
	auto result = starts_with("hello world!", "abc");
	CHECK_EQUAL(false, result);
}

//starts_with test 2
TEST(StartsWith_2, MyUnitTests)
{
	auto result = starts_with("hello world!", "hello world bye!");
	CHECK_EQUAL(false, result);
}

//starts_with test 3
TEST(StartsWith_3, MyUnitTests)
{
	auto result = starts_with("", "");
	CHECK_EQUAL(true, result);
}

//ends_with test 0
TEST(EndsWith_0, MyUnitTests)
{
	auto result = ends_with("hello world!", "rld!");
	CHECK_EQUAL(true, result);
}

//ends_with test 1
TEST(EndsWith_1, MyUnitTests)
{
	auto result = ends_with("hello world!", "abc");
	CHECK_EQUAL(false, result);
}

//ends_with test 2
TEST(EndsWith_2, MyUnitTests)
{
	auto result = ends_with("hello world!", "hello world bye!");
	CHECK_EQUAL(false, result);
}

//ends_with test 3
TEST(EndsWith_3, MyUnitTests)
{
	auto result = ends_with("", "");
	CHECK_EQUAL(true, result);
}


//IsCommentLine test 0
TEST(IsCommentLine_0, MyUnitTests)
{
	string input = "			//This function adds two numbers and returns the result";
	auto result = IsCommentLine(input);
	CHECK_EQUAL(true, result);
}

//IsCommentLine test 1
TEST(IsCommentLine_1, MyUnitTests)
{
	string input = "void add(int a, int b) { //add here...\n}";
	auto result = IsCommentLine(input);
	CHECK_EQUAL(false, result);
}

//IsCommentLine test 2
TEST(IsCommentLine_2, MyUnitTests)
{
	string input = "";
	auto result = IsCommentLine(input);
	CHECK_EQUAL(false, result);
}

//IsFunctionDefinition_4 Member functions
TEST(IsFunctionDefinition_4, MyUnitTests)
{
	string input =
		"void CMainFrame::OnViewCustomize() \tconst\t\n"
		"{\n"
		"\tCMFCToolBarsCustomizeDialog* pDlgCust = new CMFCToolBarsCustomizeDialog(this, TRUE /* scan menus */);\n"
		"\tpDlgCust->EnableUserDefinedToolbars();\n"
		"\tpDlgCust->Create();\n"
		"}\n";

	auto result = IsFunctionDefinition(input);
	CHECK_EQUAL(true, result);
}

//IsFunctionDefinition_4 Member functions
TEST(GetFunctionInfo_3, MyUnitTests)
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
	CHECK(expected == result);
}



//IsFunctionDefinition_4 Member functions
TEST(Match_Declaration_With_Definition, MyUnitTests)
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

	LONGS_EQUAL(1, func_decls.size());					//found 1 declaration
	LONGS_EQUAL(2, func_defs.size());						//found 2 definitions
	LONGS_EQUAL(3, func_decls.begin()->first);	//declaration is at line 3
	LONGS_EQUAL(7, func_defs.begin()->first);		//first definition is at line 5
	LONGS_EQUAL(12, func_defs.rbegin()->first);	//second definition is at line 10

	auto func_parts = split(func_defs[12].name, ':', true);  //Split member function name using "::" to in order to get the function name
	auto func_def = func_parts[1];
	CHECK(func_decls.begin()->second.name == func_def);			//Match the declaration function name with the definition function name
}
