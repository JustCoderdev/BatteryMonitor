
#include <core.h>

#include <stdio.h>

int main(int argc, char** argv)
{
	char* program = shift(&argc, &argv);
	(void)program;

	printf("Hello World!\n");

	return success;
}
