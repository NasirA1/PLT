#include <stdio.h>
#include "TestHarness.h"

//Tests
#include "IsFunctionDeclaration_Tests.h"
#include "IsFunctionDefinition_And_Other_Tests.h"



int main(int argc, char* argv[])
{
	printf("%s", "Tests starting now...\n");
	TestResult tr;
	TestRegistry::runAllTests(tr);

	getchar();
	return 0;
}
