#include "request.h"
#include "server_thread.h"
#include "common.h"
#include <pthread.h>

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
	pthread_t* thread_array;
	int* buffer;

	//This version is exactly the same as the producer-consumer in lecture
	int in;
	int out;
};

pthread_cond_t cv_producer;
pthread_cond_t cv_consumer;
pthread_mutex_t buffer_lock;
pthread_mutex_t cache_lock;

struct ht_element {
	struct file_data* data;
	int count;
	int valid;
};
struct ht_element* hash_table;
int ht_size; //total number of hash table elements
int ht_remaining; //bytes of hash table size remaining

int flag; //there are some test cases where cache_size is small. No ht in this case

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

/* static functions */
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

void cache_insert(struct file_data* data){

	if (!flag) //no hash table
		return;
	//get the hash
	int index = hashFunction(data -> file_name) % ht_size;

	//move in hashtable
	while (hash_table[index].valid != -1){
		if (strcmp(hash_table[index].data -> file_name, data -> file_name) == 0){
			//shouldn't happen. A better way is synchro
			return;
		}

		//go to next one
		index++;

		//wrap around
		index = index % ht_size;
	}

	//
	hash_table[index].data = data;
	hash_table[index].valid  = 1;
	hash_table[index].count = 0;
	ht_remaining -= data -> file_size;
}

int cache_lookup(struct file_data* data){
	
	if (!flag) //no hash table
		return -1;
	//get the hash
	int index = hashFunction(data -> file_name) % ht_size;
	int count = 0;

	//move in hashtable
	while (hash_table[index].valid != -1){

		//to prevent if all elements are valid but no lookup
		if (count > ht_size)
			return -1;

		if (strcmp(hash_table[index].data -> file_name, data -> file_name) == 0){
			return index;
		}

		//go to next one
		index++;

		//wrap around
		index = index % ht_size;

		count++;
	}

	return -1;
}

//the only place we call file_free
//you can obviously use a more sophisticated replacement policy
//but hash tables are random anyways
void cache_replace(int target_size){

	if (!flag) //no hash table
		return;
	int index = 0;
	while (index < ht_size && ht_remaining < target_size){
		if (hash_table[index].valid != -1 && hash_table[index].count == 0){
			ht_remaining += hash_table[index].data -> file_size;
			file_data_free(hash_table[index].data);
			hash_table[index].valid = -1;
		}

		index++;
	}
}


static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();

	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file,
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	pthread_mutex_lock(&cache_lock);
	int index;
	if (flag && (index = cache_lookup(data)) != -1){
		data = hash_table[index].data;
		request_set_data(rq, data);
	} else {
		pthread_mutex_unlock(&cache_lock);
		ret = request_readfile(rq);
		if (ret == 0) /* couldn't read file */
			goto out;
		pthread_mutex_lock(&cache_lock);
		
		if (flag && data -> file_size < sv -> max_cache_size / 2){
			if (data -> file_size > ht_remaining)
				cache_replace(data -> file_size);
			if (data -> file_size < ht_remaining)
				cache_insert(data);
		}
		
	}

	if (flag && (index = cache_lookup(data)) != -1)
		hash_table[index].count++;
	pthread_mutex_unlock(&cache_lock);

	/* send file to client */
	request_sendfile(rq);
out:
	
	pthread_mutex_lock(&cache_lock);
	if (flag && index != -1)
		hash_table[index].count--;
	pthread_mutex_unlock(&cache_lock);

	if (!flag || index == -1){
		file_data_free(data);
	}
	request_destroy(rq);
}


void 
thread_stub(struct server* sv)
{
	while (1){
		pthread_mutex_lock(&buffer_lock);

		//Exactly the same as the waking condition in the other loop
		while (sv -> in == sv -> out){
			if (sv -> exiting){
				//
				pthread_mutex_unlock(&buffer_lock);
				pthread_exit(0);
				return;
			}
			pthread_cond_wait(&cv_consumer, &buffer_lock);
		}

		//max_requests is actually important to enforce because it might be 1
		//Interestingly, broadcast is much faster than signal
		if ((sv -> in - sv -> out + sv -> max_requests + 1)%(sv -> max_requests + 1) == sv -> max_requests)
			pthread_cond_broadcast(&cv_producer);

		int j = sv -> buffer[sv -> out];
		sv -> out = (sv -> out + 1)%(sv -> max_requests + 1);
		pthread_mutex_unlock(&buffer_lock);
		do_server_request(sv, j);
	}

	//should never get here
	pthread_exit(0);
}

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;
	
	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		
		pthread_cond_init(&cv_producer, NULL);
		pthread_cond_init(&cv_consumer, NULL);
		pthread_mutex_init(&buffer_lock, NULL);
		pthread_mutex_init(&cache_lock, NULL);
		sv -> in = 0;
		sv -> out = 0;
		sv -> buffer = Malloc((max_requests+1) * sizeof(int));
		sv -> thread_array = Malloc(nr_threads * sizeof(pthread_t));

		for (int i = 0; i < nr_threads; i++)
			pthread_create(sv -> thread_array + i, NULL, (void*)thread_stub, sv);
		ht_remaining = max_cache_size;
		ht_size = max_cache_size/5000;
		if (ht_size < 2)
			flag = 0;
		else{
			hash_table = (struct ht_element*)Malloc(sizeof(struct ht_element)*ht_size);
			for (int i = 0; i < ht_size; i++){
				hash_table[i].valid = -1;
				hash_table[i].count = 0;
			}
			flag = 1;
		}
	}

	/* Lab 4: create queue of max_request size when max_requests > 0 */

	/* Lab 5: init server cache and limit its size to max_cache_size */

	/* Lab 4: create worker threads when nr_threads > 0 */

	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&buffer_lock);

		//max_requests is actually important to enforce because it might be 1
		while ((sv -> in - sv -> out + sv -> max_requests + 1)%(sv -> max_requests + 1) == sv -> max_requests)
			pthread_cond_wait(&cv_producer, &buffer_lock);

		//Exactly the same as the sleeping condition in the other loop
		//Interestingly, broadcast is much faster than signal
		if (sv -> in  == sv -> out)
			pthread_cond_broadcast(&cv_consumer);

		sv -> buffer[sv -> in] = connfd;
		sv -> in = (sv -> in + 1)%(sv -> max_requests + 1);
		pthread_mutex_unlock(&buffer_lock);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	pthread_mutex_lock(&buffer_lock);
	sv -> exiting = 1;

	//
	pthread_mutex_unlock(&buffer_lock);
	for (int i = 0; i < sv -> nr_threads; i++){
		pthread_cond_broadcast(&cv_consumer);
		pthread_cond_broadcast(&cv_producer);
		assert(!pthread_join(sv -> thread_array[i], NULL));
	}

	/* make sure to free any allocated resources */
	free(sv -> thread_array);
	free(sv -> buffer);
	free(sv);

	int index = 0;
	while (index < ht_size){
		if (hash_table[index].valid != -1){
			assert(hash_table[index].count == 0);
			file_data_free(hash_table[index].data);
		}
		index++;
	}
	free(hash_table);
	//
	//pthread_mutex_unlock(&lock);
}
