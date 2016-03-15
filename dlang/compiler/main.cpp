#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <memory>
#include <fstream>
#include <stack>
#include <queue>
#include <iomanip>

#define THROW(ssargs) do {	std::ostringstream ss; ss << ssargs; throw ss.str(); } while(0)

using namespace std;


#pragma region Utilities
namespace util
{
	string readAllText(const char* const filename)
	{
		std::ifstream t(filename);
		std::string str;

		t.seekg(0, std::ios::end);
		str.reserve(static_cast<unsigned int>(t.tellg()));
		t.seekg(0, std::ios::beg);

		str.assign((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
		return str; //C++11: move the string
	}

	void writeBinary(const char* const filename, const vector<unsigned char>& bin)
	{
		std::ofstream file(filename, ios::out | ios::binary);
		std::copy(bin.begin(), bin.end(), std::ostreambuf_iterator<char>(file));
	}

	vector<unsigned char> readBinary(const char* const filename)
	{
		std::ifstream file(filename, std::ios::in | std::ifstream::binary);
		std::vector<unsigned char> contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return contents;
	}
}
#pragma endregion



namespace dlang
{

	#pragma region Parsing
	
	//NUM, (, ), +, -, *, / 
	enum TokenType
	{
		TOKEN_INVALID,
		TOKEN_EOF,
		TOKEN_NUM,
		TOKEN_LPAREN,
		TOKEN_RPAREN,
		TOKEN_ADD,
		TOKEN_SUB,
		TOKEN_MUL,
		TOKEN_DIV
	};

	
	enum ASSOCIATIVITY
	{
		ASSOC_LEFT,
		ASSOC_RIGHT
	};

	
	struct token_info
	{
		const string name;
		const unsigned char prec;
		const ASSOCIATIVITY assoc;
	};

	
	const token_info tokenInfo[] = {
			{ "n/a", 0xFF, ASSOC_LEFT },
			{ "<EOF>", 0xFF, ASSOC_LEFT },
			{ "NUM", 0xFF, ASSOC_LEFT },
			{ "LPAREN", 0xFF, ASSOC_LEFT },
			{ "RPAREN", 0xFF, ASSOC_LEFT },
			{ "ADD", 3, ASSOC_LEFT },
			{ "SUB", 3, ASSOC_LEFT },
			{ "MUL", 2, ASSOC_LEFT },
			{ "DIV", 2, ASSOC_LEFT }
	};

	
	string getTokenName(const TokenType id)
	{
		return tokenInfo[(int)id].name;
	}

	
	unsigned char getTokenPrec(const TokenType id)
	{
		return tokenInfo[(int)id].prec;
	}

	
	ASSOCIATIVITY getTokenAssoc(const TokenType id)
	{
		return tokenInfo[(int)id].assoc;
	}

	
	bool isOperator(const TokenType id)
	{
		return (int)id > 4;
	}

	
	struct token
	{
		TokenType type;
		string text;

		token(const TokenType ty = TOKEN_INVALID, const string& tx = "")
			: type(ty)
			, text(tx)
		{}

		string toString() const
		{
			ostringstream ss;
			ss << "<'" << text << "'," << getTokenName(type) << ">";
			return ss.str();
		}
	};

	
	class lexer
	{
		string input;
		int p = 0;
		char c;

	public:
		lexer(const string& in)
			: input(in)
			, p(0)
			, c(in[p])
		{
		}


		void consume()
		{
			p++;
			if ((unsigned int)p >= input.length()) c = EOF;
			else c = input[p];
		}


		void match(char x)
		{
			if (c == x)
				consume();
			else
				THROW("LEXER: Expecting '" << x << "', Found '" << c << "'");
		}


		void WS()
		{
			while (c == ' ' || c == '\t' || c == '\n' || c == '\r')
				consume();
		}


		token NUM()
		{
			ostringstream ss;
			do {
				ss << c;
				consume();
			} while (::isdigit(c));

			return token(TOKEN_NUM, ss.str());
		}


		token nextToken()
		{
			while (c != EOF)
			{
				switch (c)
				{
				case ' ': case '\t': case '\n': case '\r':
					WS();
					continue;
				case '(':
					consume();
					return token(TOKEN_LPAREN, "(");
				case ')':
					consume();
					return token(TOKEN_RPAREN, ")");
				case '+':
					consume();
					return token(TOKEN_ADD, "+");
				case '-':
					consume();
					return token(TOKEN_SUB, "-");
				case '*':
					consume();
					return token(TOKEN_MUL, "*");
				case '/':
					consume();
					return token(TOKEN_DIV, "/");
				default:
					if (::isdigit(c))
						return NUM();
					else
						THROW("LEXER: Invalid character '" << c << "'");
				}
			}

			return token(TOKEN_EOF, "<EOF>");
		}
	};

	
	struct EXPR
	{
		using ptr = unique_ptr < EXPR >;
		token tok;
		ptr left, right;

		EXPR(){}
		EXPR(const token& t) : tok(t), left(nullptr), right(nullptr) {}
		EXPR(const TokenType ttype) : tok(ttype, getTokenName(ttype)), left(nullptr), right(nullptr) {}
		//~EXPR() { cout << "AUTO .DTOR" << endl; }
	};

	
	void printEXPR(const EXPR::ptr& tree, int indent = 2)
	{
		if (tree)
		{
			cout << string(indent, '.') << (isOperator(tree->tok.type) ? ">> " : ".. ") << tree->tok.text << endl;
			printEXPR(tree->left, indent + 2);
			printEXPR(tree->right, indent + 2);
		}
	}

	
	class parser
	{
		lexer& input;
		vector<token> lookaheads;
		stack<int> markers;
		int p;

			
	public:
		EXPR::ptr tree;


		parser(lexer& in)
			: input(in)
			, p(0)
		{
			lookaheads.reserve(100);
		}

		
		void consume()
		{
			p++;

			//if we hit end of buffer when not backtracking
			if (p == lookaheads.size() && !isSpeculating())
			{
				//rewind to the beginning
				p = 0;
				lookaheads.clear();
			}

			//get another token to replace consumed token
			sync(1);
		}


	private:
		token LT(int i) { sync(i); return lookaheads[p + i - 1]; }
		TokenType LA(int i) { return LT(i).type; }


		//Make sure we have i tokens from current position ps
		void sync(int i)
		{
			if (p + i - 1 > (int)(lookaheads.size() - 1))
			{
				int n = (p + i - 1) - (lookaheads.size() - 1); //get n tokens
				fill(n);
			}
		}


		int mark()
		{
			markers.push(p);
			return p;
		}


		void release()
		{
			int marker = markers.top();
			markers.pop();
			seek(marker);
		}


		void seek(int index) { p = index; }


		void fill(int n) //add n tokens
		{
			for (int i = 1; i <= n; i++)
				lookaheads.push_back(input.nextToken());
		}


		bool isSpeculating()
		{
			return markers.size() > 0;
		}

		
		void match(const TokenType x)
		{
			if (LA(1) == x) consume();
			else
				THROW("PARSER: Expecting " << getTokenName(x) << " Found: " << LT(1).toString());
		}


		//void stat()
		//{
		//	if (speculate_stat_alt1())
		//	{
		//		list();
		//		match(TOKEN_EOF);
		//	}
		//	else if (speculate_stat_alt2())
		//	{
		//		assign();
		//		match(TOKEN_EOF);
		//	}
		//	else
		//	{
		//		THROW("PARSER: Expecting stat; found " << LT(1).toString());
		//	}
		//}


		//bool speculate_stat_alt1()
		//{
		//	bool success = true;
		//	mark();  //mark so we can rewind
		//	try{
		//		list();
		//		match(TOKEN_EOF);
		//		cout << "Parsed list" << endl;
		//	}
		//	catch (string&)
		//	{
		//		success = false;
		//	}

		//	release(); //either way rewind to where we were
		//	return success;
		//}


		//bool speculate_stat_alt2()
		//{
		//	bool success = true;
		//	mark();  //mark so we can rewind
		//	try{
		//		assign();
		//		match(TOKEN_EOF);
		//		cout << "Parsed assign" << endl;
		//	}
		//	catch (string&)
		//	{
		//		success = false;
		//	}

		//	release(); //either way rewind to where we were
		//	return success;
		//}


	public:
		/*
		Dijkstra's Shunting-yard Algorithm implementation https://en.wikipedia.org/wiki/Shunting-yard_algorithm
		The input is processed one symbol at a time, if a variable or number is found it is copied directly to the output.
		If the symbol is an operator, it is pushed onto the operator stack. If the operator's precedence is less than that of
		the operator at the top of the stack or the precedences are equal and the operator is left associative then that operator is
		popped off the stack and added to the output. Finally any remaining operators are popped off the stack and added to the
		output.
		*/
		void parse()
		{
			stack<token> ops;
			stack<EXPR::ptr> output;
			bool handleNegative = (LA(1) == TOKEN_SUB);

#define CREATE_EXPR() \
			do { \
				EXPR::ptr node(new EXPR(ops.top().type)); \
				node->right = std::move(output.top()); \
				output.pop(); \
				node->left = std::move(output.top()); \
				output.pop(); \
				output.push(std::move(node)); \
				ops.pop(); \
			} while(0)

			while (LA(1) != TOKEN_EOF)
			{
				if (LA(1) == TOKEN_NUM)
				{
					output.push(EXPR::ptr(new EXPR(LT(1))));
					consume();
					handleNegative = false;
				}
				else if (LA(1) == TOKEN_LPAREN)
				{
					ops.push(LT(1));
					consume();
					handleNegative = true;
				}
				else if (LA(1) == TOKEN_RPAREN)
				{
					bool lparent_found = false;
					while (!ops.empty())
					{
						if (ops.top().type == TOKEN_LPAREN)
						{
							ops.pop();
							lparent_found = true;
							break;
						}
						else
						{
							CREATE_EXPR();
						}
					}
					if (!lparent_found)
						THROW("PARSER: Mismatched parentheses");
					consume();

					handleNegative = false;
				}
				else
				{
					if (handleNegative)
					{
						if (LA(1) == TOKEN_SUB)
						{
							consume();
							token t(TOKEN_NUM, "-" + LT(1).text);
							output.push(EXPR::ptr(new EXPR(t)));
							consume();
							handleNegative = false;
							continue;
						}
						else
						{
							THROW("PARSER: Syntax error");
						}
					}
					else
					{
						handleNegative = true;
					}

					/* operators */
					if (ops.empty())
					{
						ops.push(LT(1));
						consume();
					}
					else
					{
						while (!ops.empty() && isOperator(ops.top().type) &&
							((getTokenAssoc(LA(1)) == ASSOC_LEFT && getTokenPrec(LT(1).type) >= getTokenPrec(ops.top().type)) ||
							(getTokenAssoc(LA(1)) == ASSOC_RIGHT && getTokenPrec(LT(1).type) > getTokenPrec(ops.top().type))
							)
						)
						{
							CREATE_EXPR();
						}
						ops.push(LT(1));
						consume();
						handleNegative = true;
					}
				}
			}

			//add remaining operators
			while (!ops.empty())
			{
				token op = ops.top();
				if (op.type == TOKEN_LPAREN || op.type == TOKEN_RPAREN)
					THROW("PARSER:  Mismatched parentheses");
				CREATE_EXPR();
			}

			tree = std::move(output.top());
			output.pop();
			//cout << "AST built." << endl << endl;
#undef CREATE_EXPR
		}

	};

#pragma endregion

	
	#pragma region Code Generation

	namespace {
		using byte = unsigned char;
		enum bytecode        { NOOP, ADD, SUB, MUL, DIV, LOADLIT };
		string mnemonics[] = { "NOOP", "ADD", "SUB", "MUL", "DIV", "LOADLIT" };
	}

	
	class code_generator
	{
	public:
		//Contains generated machine code
		vector<byte> code;


		//Generates machine code from the given AST
		void generate(const EXPR::ptr& tree)
		{
			flatten(tree);
			std::reverse(begin(code), end(code));
		}


		//Reverse-engineers machine code into assembly
		vector<string> assembly()
		{
			vector<string> result;

			for (auto it = code.begin(); it != code.end(); ++it)
			{
				if (*it == bytecode::LOADLIT)
				{
					const string instruction = mnemonics[*it];
					byte b1 = *++it;
					byte b2 = *++it;
					byte b3 = *++it;
					byte b4 = *++it;
					int value = (b1 & 0xFF) | (b2 & 0xFF) << 8 | (b3 & 0xFF) << 16 | (b4 & 0xFF) << 24;
					result.push_back(instruction + "," + to_string(value));
				}
				else
				{
					result.push_back(mnemonics[*it]);
				}
			}

			return result;
		}


	private:
		//Recursive function
		//Walks the AST depth-first, generating machine code in the process
		void flatten(const EXPR::ptr& tree)
		{
			if (!tree) return;

			if (tree->tok.type == TOKEN_NUM)
			{
				//cout << "tok.text=<" << tree->tok.text << ">" << endl;
				int value = stoi(tree->tok.text);
				//Little endian
				code.push_back((value >> 24) & 0xFF);
				code.push_back((value >> 16) & 0xFF);
				code.push_back((value >> 8) & 0xFF);
				code.push_back(value & 0xFF);
				code.push_back(bytecode::LOADLIT);
			}
			else
			{
				switch (tree->tok.type)
				{
				case TOKEN_ADD:
					code.push_back(bytecode::ADD);
					break;
				case TOKEN_SUB:
					code.push_back(bytecode::SUB);
					break;
				case TOKEN_MUL:
					code.push_back(bytecode::MUL);
					break;
				case TOKEN_DIV:
					code.push_back(bytecode::DIV);
					break;
				default:
					THROW("Unrecongised token " << tree->tok.type);
				}
				flatten(tree->left);
				flatten(tree->right);
			}
		}
	};

	
	void printcode(const vector<byte>& bytes)
	{
		int col = 0;
		for (auto b : bytes)
		{
			cout << setfill('0') << setw(2) << hex << (int)b;
			if (col < 7)
			{
				cout << " ";
				col++;
			}
			else
			{
				cout << endl;
				col = 0;
			}
		}
	}

	
	vector<byte> compile(const string& sourcecode)
	{
		lexer lex(sourcecode);
		parser par(lex);
		code_generator codegen;

		//cout << "Source code: " << endl;
		//cout << sourcecode << endl << endl;

		cout << "Compiling... " << endl;
		par.parse();
		codegen.generate(par.tree);

		cout << "Syntax tree:" << endl;
		printEXPR(par.tree);

		cout << endl << "Assembly:" << endl;
		vector<string> ax = codegen.assembly();
		for_each(begin(ax), end(ax), [](const string& s){ cout << s << endl; });

		cout << endl << "Machine code:" << endl;
		printcode(codegen.code);
		cout << endl;

		return codegen.code;
	}

#pragma endregion


}//end-namespace




int main(int argc, char* argv[])
{
	using namespace dlang;

	istringstream ss;
	
	if (argc < 2)
	{
		cout << "Give me an arithmetic expression." << endl;
		return -1;
	}

	try
	{
		vector<byte> machinecode = compile(argv[1]);
		
		try
		{
			util::writeBinary("out.d", machinecode);
			cout << "\nCompiled successfully to 'out.d'" << endl << endl;
		}
		catch (exception& e)
		{
			cout << e.what() << endl;
			return -1;
		}

		return 0;
	}
	catch (string& e)
	{
		cout << e << endl;
		return -1;
	}
}
