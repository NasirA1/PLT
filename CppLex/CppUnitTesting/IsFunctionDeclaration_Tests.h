#pragma once
#include "TestHarness.h"
#include "CppLex.h"

using namespace std;


#if 0
//Test template functions
//No need for template functions because they are all define inline
TEST(NotSupported_1, MyUnitTests)
{
	string input = "template<class T> T min(const T& a, const T& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Basic test (b)
//Implicit return types not supported currently
//Ambiguous whether declaration or function-call
TEST(NotSupported_0, MyUnitTests)
{
	string input = "foo();";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}
#endif


//Negative test 2 - function definition
TEST(NOT_FunctionDeclaration_2, MyUnitTests)
{
	string input = "void main(int argc, char* argv[])\n{ return 0; }";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(false, result);
}

//Negative test 1 - struct declaration
TEST(NOT_FunctionDeclaration_1, MyUnitTests)
{
	string input = "struct Foo";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(false, result);
}

//Negative test 0 - class declaration
TEST(NOT_FunctionDeclaration_0, MyUnitTests)
{
	string input = "class Foo";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(false, result);
}

//Operator overloads
#if 0 //might do
TEST(IsFunctionDeclaration_xx, MyUnitTests)
{
	string input = "ostream& operator<< (const ostream& out, const Foo& obj);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}
#endif


//namespaces
TEST(IsFunctionDeclaration_12, MyUnitTests)
{
	string input = "std::string& trim(std::string& s);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}


//static inline functions
TEST(IsFunctionDeclaration_11b, MyUnitTests)
{
	string input = "inline friend static char* swap(blah a, blah y, blah zz);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//static inline functions
TEST(IsFunctionDeclaration_11, MyUnitTests)
{
	string input = "static inline void swap(const int& a, const int& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//friend functions
TEST(IsFunctionDeclaration_10, MyUnitTests)
{
	string input = "friend void swap(const int& a, const int& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test pointer references
TEST(IsFunctionDeclaration_9, MyUnitTests)
{
	string input = "int&* foo(const int*& a, const float**& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test universal references
TEST(IsFunctionDeclaration_8, MyUnitTests)
{
	string input = "int&& foo(const int&& a, const float&& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test pointers to pointers
TEST(IsFunctionDeclaration_7, MyUnitTests)
{
	string input = "int** foo(const int** a, const float** b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test reference return types
TEST(IsFunctionDeclaration_6, MyUnitTests)
{
	string input = "int& min(const T& a, const T& b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test references params
TEST(IsFunctionDeclaration_5, MyUnitTests)
{
	//string input = "int min(const T& a, const T& b);";
	string input = "int min(int a, int b);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test const return type (b)
TEST(IsFunctionDeclaration_4b, MyUnitTests)
{
	string input = "const char* const strstr(const char* const hackstack, const char* const needle);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test const return type
TEST(IsFunctionDeclaration_4, MyUnitTests)
{
	string input = "const char* strstr(const char* const hackstack, const char* const needle);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test const parameters
TEST(IsFunctionDeclaration_3, MyUnitTests)
{
	string input = "char* strstr(char* const hackstack, char* const needle);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test pointer params
TEST(IsFunctionDeclaration_2, MyUnitTests)
{
	string input = "char* strstr(char* hackstack, char* needle);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test no params (b)
TEST(IsFunctionDeclaration_1c, MyUnitTests)
{
	string input = "void main( void );";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test no param names
TEST(IsFunctionDeclaration_1b, MyUnitTests)
{
	string input = "int add(int,int);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Test no params
TEST(IsFunctionDeclaration_1, MyUnitTests)
{
	string input = "void main();";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}

//Basic test
TEST(IsFunctionDeclaration_0, MyUnitTests)
{
	string input = "void main (int argc, char* argv[]);";
	auto result = IsFunctionDeclaration(input);
	CHECK_EQUAL(true, result);
}
