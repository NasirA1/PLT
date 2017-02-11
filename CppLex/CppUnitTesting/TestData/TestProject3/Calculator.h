#pragma once

//Class Calculator represents a calculator with a capability of performing arithmetic
class Calculator
{
public:
	//Constructs a Calculator object
	Calculator();
	
	//Calculator Destructor
	~Calculator();
	
	//Adds a and b and returns the result
	//Returns an int
	int Add(const int a, const int b);
	
	

	//Subtracts b from a and returns the result in an int
	int Sub(int a, int b);		
	
	//This line MUST not appear in the CPP!
	
	//Multiplies a and b and returns the result
	//Returns an int
	int Mul(int a, int b);
	
	//Divides a by b
	//Returns an int
	//Throws a division-by-zero exception if b is set to zero!
	int Div(int a, int b);
};
