#include <string>
#include <iostream>
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")


using namespace std;

namespace util
{
	string GetThisPath()
	{
		CHAR path[MAX_PATH] = { 0 };
		DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
		PathRemoveFileSpecA(path);
		return path;
	}

	void run(const string& command)
	{
		STARTUPINFOA si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));

		// Start the child process. 
		if (!CreateProcessA(NULL,   // No module name (use command line)
			(char*)command.c_str(),		// Command line
			NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL,           // Use parent's starting directory 
			&si,            // Pointer to STARTUPINFO structure
			&pi)           // Pointer to PROCESS_INFORMATION structure
			)
		{
			printf("CreateProcess failed (%d).\n", GetLastError());
			return;
		}

		// Wait until child process exits.
		WaitForSingleObject(pi.hProcess, INFINITE);

		// Close process and thread handles. 
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
}





int main(int argc, char* argv[])
{
	using namespace util;
	const string program_dir = GetThisPath();
	const string sourcecode = "20 * 15 + ((450 / 15) - (2 * (11 - 5)) + 10)";
	//const string sourcecode = "20bbb"; //syntax error

	//compile the sourecode	
	run("dlangc.exe \"" + sourcecode + "\"");

	//run on the interpreter
	run("dlang.exe out.d");

#if _DEBUG
cin.get();
#endif
}
