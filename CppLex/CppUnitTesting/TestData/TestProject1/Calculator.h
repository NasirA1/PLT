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
};
