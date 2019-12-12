#include "common.h"

int
main(int argc, char* argv[])
{
	//first argument is just program name
	int i = 1;
	while (argv[i] != '\0'){	
		printf("%s\n", argv[i]);
		i++;
	}
	return 0;
}
