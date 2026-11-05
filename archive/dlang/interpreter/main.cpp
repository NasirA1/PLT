#include <iostream>
#include <vector>
#include <stack>
#include <sstream>
#include <fstream>

#define THROW(ssargs) do {	std::ostringstream ss; ss << ssargs; throw ss.str(); } while(0)

using namespace std;



namespace util
{
	vector<unsigned char> readBinary(const char* const filename)
	{
		std::ifstream file(filename, std::ios::in | std::ifstream::binary);
		std::vector<unsigned char> contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return contents;
	}
}


namespace dlang
{
	using byte = unsigned char;
	enum bytecode        { NOOP, ADD, SUB, MUL, DIV, LOADLIT };
	string mnemonics[] = { "NOOP", "ADD", "SUB", "MUL", "DIV", "LOADLIT" };

	
	class interpreter
	{
	public:
		stack<int> memory;


		void reset()
		{
			while (!memory.empty())
				memory.pop();
		}


		void execute(vector<byte>& program)
		{
			reset();

			for (auto it = program.begin(); it != program.end(); ++it)
			{
				if (*it == bytecode::LOADLIT)
				{
					const string instruction = mnemonics[*it];
					byte b1 = *++it;
					byte b2 = *++it;
					byte b3 = *++it;
					byte b4 = *++it;
					int value = (b1 & 0xFF) | (b2 & 0xFF) << 8 | (b3 & 0xFF) << 16 | (b4 & 0xFF) << 24;
					memory.push(value);
				}
				else
				{
					int a = memory.top();
					memory.pop();
					int b = memory.top();
					memory.pop();

					bytecode instruction = (bytecode)*it;
					int result = 0;

					switch (instruction)
					{
					case bytecode::ADD:
						result = a + b;
						break;
					case bytecode::SUB:
						result = a - b;
						break;
					case bytecode::MUL:
						result = a * b;
						break;
					case bytecode::DIV:
						if (b > 0)
							result = a / b;
						else
							THROW("\nRuntime Error: Division by zero");
						break;
					default:
						THROW("*** CRASH *** INVALID OPERATION! ***");
					}

					memory.push(result);
				}
			}
		}
	};

}//end-namespace




int main(int argc, char* argv[])
{
	using namespace dlang;

	if (argc < 2)
	{
		cout << "Give me an executable." << endl;
		return -1;
	}

	try
	{
		vector<byte> machinecode = util::readBinary(argv[1]);
		if (machinecode.size() <= 0) {
			cout << "Nothing to execute." << endl;
			return -1;
		}

		try
		{
			interpreter vm;
			cout << "Executing '"<<argv[1]<<"' on virtual machine..." << endl;
			vm.execute(machinecode);
			cout << "Executed successfully." << endl;
			cout << "Result: " << vm.memory.top() << endl;
		}
		catch (string& e)
		{
			cout << e << endl;
			return -1;
		}

		return 0;
	}
	catch (exception& e)
	{
		cout << e.what() << endl;
		return -1;
	}
}

