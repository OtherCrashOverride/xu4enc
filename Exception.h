#pragma once

#include <exception>

class Exception : public std::exception
{
public:
	Exception(const char* message)
		: std::exception()
	{
		fprintf(stderr, "%s\n", message);
	}

};
