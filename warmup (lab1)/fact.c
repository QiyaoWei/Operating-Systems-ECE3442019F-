//This program doesn't seem to work for input larger than 10 digits

#include "common.h"

int fact(int a);

int
main(int argc, char* argv[])
{
	char* end;
	strtol(argv[1], &end, 10);
	if (*end != '\0' || *(argv[1]) == '-' || *(argv[1]) == '0') //I don't know why strtol automatically takes care of decimal point cases, but not negative numbers or 0
		printf("Huh?\n");
	else if (atoi(argv[1]) > 12)
		printf("Overflow\n");
	else
		printf("%d\n", fact(atoi(argv[1])));
	return 0;
}

int fact(int a){
	if (a <= 1)
		return 1;
	return fact(a-1) * a;
}
