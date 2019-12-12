#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "common.h"
#include "wc.h"

//forward decs
unsigned long hashFunction(char* str); //Investigate: is there a better hash?
void insert (struct wc* wc, char* input);

//helper struct
struct w {
	char input[70]; //70 is the magic number
	int data;
};

struct wc {
	/* you can define this struct to have whatever fields you want. */
	struct w* hashTable;
	//how stupid. Seems like there could be a smarter way to do this
	long size;
};

//---------------------------------------------------------------------
struct wc *
wc_init(char *word_array, long size)
{
	//"a good rule of thumb" is size/2. But that would be too much memory
	struct wc* wc   = (struct wc*)malloc(sizeof(struct wc));
	wc -> size = size/4;
	wc -> hashTable = (struct w*)malloc(sizeof(struct w)*(wc -> size));

	if (wc -> hashTable){ //checking if malloc is successful
		//Initializing is always a good idea
		for (int i = 0; i < wc -> size; i++)
			wc -> hashTable[i].data = 0;
	}

	//The trick is: '\0' is always the end of a string, regardless of what follows after
	char temp[70];
	int i = 0;
	int j = 0;

	//get to the first non-space char
	while(isspace(word_array[i])){
		i++;
	}

	while(word_array[i] != '\0'){

		assert (j < 70);
		temp[j] = word_array[i];
		i++;
		j++;

		if (isspace(word_array[i])){
			temp[j] = '\0';
			insert(wc, temp);
			j = 0;
			//get to the next non-space char
			i++;
			while(isspace(word_array[i])){
				i++;
			}
		}
	}

	//send out the last word.
	if (j > 0){
		temp[j] = '\0';
		insert(wc, temp);
	}

	return wc;
}

//http://www.cse.yorku.ca/~oz/hash.html : djb2 hash
unsigned long hashFunction(char* str){
	unsigned long hash = 5381;
	int c = *str++;
	while (c){
		hash = ((hash << 5) + hash) + c; //hash * 33 + c
		c = *str++;
	}

	return hash;
}

void insert (struct wc* wc, char* input){

	//get the hash
	int index = hashFunction(input) % (wc -> size);

	//move in hashtable
	while (wc -> hashTable[index].data != 0){
		if (strcmp(wc -> hashTable[index].input, input) == 0){
			wc -> hashTable[index].data++;
			return;
		}

		//go to next one
		index++;

		//wrap around
		index = index % (wc -> size);
	}

	//*** Notice strcpy
	strcpy(wc -> hashTable[index].input, input);
	wc -> hashTable[index].data  = 1;
	return;
}

void
wc_output(struct wc *wc)
{
	for (int i = 0; i < wc -> size; i++)
		if (wc -> hashTable[i].data != 0)
			printf("%s:%d\n", wc -> hashTable[i].input, wc -> hashTable[i].data);
}

void
wc_destroy(struct wc *wc)
{
	free(wc -> hashTable);
	free(wc);
}
