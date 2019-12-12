#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

/*
Some further changes:
1. Wrapper functions---
	I. Put to end of queue
*/

/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
	struct thread* head;
};

//may be useful in lab3 as well
typedef enum TStatus{
	killed,
	normal
}TStatus;

/* This is the thread control block */
struct thread {
	//lab2
	ucontext_t mycontext;
	void* sp;
	Tid tid;
	struct thread* next;
	TStatus status;

	//lab3
	struct wait_queue* wq;
};

//In lab 3, it is vital that you have a mapping from tid to TCB. I still don't regret implementing the linked list though, just for the better performance
struct thread* tcb_array[THREAD_MAX_THREADS];

//just a simple linked list for ready_queue
struct thread* head = NULL;

//and a simple linked list for exit_queue
//note that exit queue and ready queue must be separate
struct thread* done = NULL;

//keep track of available Tid
Tid tid_array[THREAD_MAX_THREADS];

//keep track of which thread is running
struct thread* current_running= NULL;

volatile int setcontext_called = 1; //doesn't really matter if it's 1 or 0

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

//added function. Allow thread to voluntarily quit, as well as clean up other threads
void
clean()
{
	interrupts_off();

	struct thread* temp = done;
	struct thread* t = NULL;
	while (temp != NULL){
		t = temp -> next;
		tid_array[temp -> tid] = temp -> tid;
		tcb_array[temp -> tid] = NULL;
		if (temp -> tid != 0){
			free(temp -> sp);
			free(temp);
		}
		temp = t;
	}
	//Investigate: why is it that when I take out this line my code no longer works?
	done = NULL;

	if(current_running -> status == killed)
		thread_exit();
}

//added function. Better organization
void
thread_stub(void (*fn) (void*), void* arg)
{
	clean();

	interrupts_on();

	fn(arg);
	thread_exit();
}

void
thread_init(void)
{
	//populate Tid array
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		tid_array[i] = i;
		tcb_array[i] = NULL;
	}

	//no stack allocating for this one
	struct thread* first_thread = (struct thread*)malloc(sizeof(struct thread));
	assert(first_thread != NULL);

	//head only points to ready_queue. This one goes directly into running
	tid_array[0] = -1;
	tcb_array[0] = first_thread;
	first_thread -> next = NULL;
	first_thread -> tid = 0;
	first_thread -> status = normal;
	first_thread -> wq = NULL;
	current_running = first_thread;
	getcontext(&(current_running -> mycontext));
}

Tid
thread_id()
{
	return current_running -> tid;
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	int j = interrupts_off();

	//get first tid that doesn't exist yet
	int index = 0;
	while (tid_array[index] == -1){
		index++;
		if (index == THREAD_MAX_THREADS){

			int k = interrupts_set(j);
			assert(!k);

			return THREAD_NOMORE;
		}
	}
	Tid t = tid_array[index];
	tid_array[index] = -1;

	struct thread* cur = (struct thread*)malloc(sizeof(struct thread));
	if (cur == NULL){

		int k = interrupts_set(j);
		assert(!k);

		return THREAD_NOMEMORY;
	}

	//
	getcontext(&(cur -> mycontext));

	//need to align sp address
	void* sp = (void*)malloc(THREAD_MIN_STACK + 16);
	if (sp == NULL){

		int k = interrupts_set(j);
		assert(!k);

		return THREAD_NOMEMORY;
	}

	cur -> sp = sp;

	void* rbp = (void*)(((long unsigned int)sp + THREAD_MIN_STACK + 15)/ 16 * 16 - 8);
	cur -> mycontext.uc_mcontext.gregs[REG_RIP] = (long unsigned int)(thread_stub);
	cur -> mycontext.uc_mcontext.gregs[REG_RSP] = (long unsigned int)(rbp);
	cur -> mycontext.uc_mcontext.gregs[REG_RDI] = (long unsigned int)(fn);
	cur -> mycontext.uc_mcontext.gregs[REG_RSI] = (long unsigned int)(parg);
	cur -> tid = t;
	cur -> next = NULL;
	cur -> status = normal;
	cur -> wq = NULL;

	assert(tcb_array[t] == NULL);
	tcb_array[t] = cur;

	//make temp point to struct
	//put in ready queue
	struct thread* temp = head;
	if (head == NULL)
		head = cur;
	else {
		while (temp -> next != NULL)
			temp = temp -> next;
		temp -> next = cur;
	}

	int k = interrupts_set(j);
	assert(!k);
	
	return cur -> tid;
}

Tid
thread_yield(Tid want_tid)
{
	//note that the interrupt state is undefined here
	int j = interrupts_off();

	if (want_tid == THREAD_SELF || want_tid == current_running -> tid) {

		int k = interrupts_set(j);
		assert(!k);

		//keep running current thread
		return current_running -> tid;

	} else if (want_tid == THREAD_ANY) {

		if (head == NULL){

			int k = interrupts_set(j);
			assert(!k);

			return THREAD_NONE;
		}

		//otherwise
		//take the first one out of the queue, and put caller into the queue

		struct thread* imp = current_running;
		current_running = head;
		head = head -> next;
		current_running -> next = NULL;

		//Investigate: why would this local variable still be valid when this function returns?
		Tid ret = current_running -> tid;

		struct thread* temp = head;
		if (head == NULL){
			head = imp;
		} else {
			while (temp -> next != NULL)
				temp = temp -> next;
			temp -> next = imp;
		}

		//suspend caller
		//don't forget setcontext_called
		setcontext_called = 0;
		getcontext(&(imp -> mycontext));
		clean();
		if (!setcontext_called){
			setcontext_called = 1;
			setcontext(&(current_running -> mycontext));
		}

		int k = interrupts_set(j);
		assert(!k);

		return ret;

	} else {
		if (want_tid < 0 || want_tid >= THREAD_MAX_THREADS){

			int k = interrupts_set(j);
			assert(!k);

			return THREAD_INVALID;

		} else if (tid_array[want_tid] != -1){

			int k = interrupts_set(j);
			assert(!k);

			return THREAD_INVALID;

		} else if (head == NULL){

			int k = interrupts_set(j);
			assert(!k);

			return THREAD_NONE;
		}

		//take THE tid out of the queue, then put caller at the end of queue

		struct thread* tmp = head;
		if (tmp -> tid == want_tid){
			head = tmp -> next;
		} else {
			while (tmp -> next -> tid != want_tid)
				tmp = tmp -> next;
			struct thread* t = tmp -> next;
			tmp -> next = t -> next;
			tmp = t;
		}

		struct thread* imp = current_running;
		current_running = tmp;
		current_running -> next = NULL;

		struct thread* ret = head;
		if (head == NULL){
			head = imp;
		} else {
			while (ret -> next != NULL)
				ret = ret -> next;
			ret -> next = imp;
		}

		//suspend caller
		//don't forget setcontext_called
		setcontext_called = 0;
		getcontext(&(imp -> mycontext));
		clean();
		if (!setcontext_called){
			setcontext_called = 1;
			setcontext(&(current_running -> mycontext));
		}

		int k = interrupts_set(j);
		assert(!k);

		return want_tid;
	}

	//should never get here
	return THREAD_FAILED;
}

bool check_end(){
	int count = 0;
	for (int i = 0; i < THREAD_MAX_THREADS; i++){
		if (tid_array[i] == -1)
			count++;
		if (count > 1)
			return false;
	}
	return true;
}

void
thread_exit()
{

	//The interrupt state could be defined here, but we will follow the thread_yield convention
	interrupts_off();

	if (check_end()){
		if (current_running -> tid != 0){
			wait_queue_destroy(current_running -> wq);
			free(current_running -> sp);
			//free(current_running);
		}

		exit(0); //system exit
	}

	thread_wakeup(current_running -> wq, 1);
	wait_queue_destroy(current_running -> wq);

	//add to exit queue. Thread is officially terminated
	current_running -> next = NULL;
	struct thread* temp = done;
	if (done == NULL)
		done = current_running;
	else {
		while(temp -> next != NULL)
			temp = temp -> next;
		temp -> next = current_running;
	}

	current_running = head;
	head = head -> next;
	current_running -> next = NULL;
	setcontext(&(current_running -> mycontext));
}

Tid
thread_kill(Tid tid)
{
	int j = interrupts_off();

	if (tid < 0 || tid >= THREAD_MAX_THREADS || tid_array[tid] != -1 || tid == current_running -> tid){

		int k = interrupts_set(j);
		assert(!k);

		return THREAD_INVALID;
	}

	struct thread* temp = tcb_array[tid];
	temp -> status = killed;

	int k = interrupts_set(j);
	assert(!k);

	return temp -> tid;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{

	int j = interrupts_off();

	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	wq -> head = NULL;

	int k = interrupts_set(j);
	assert(!k);

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	//You are guaranteed that the wait queue is empty. Unless it's NULL

	int j = interrupts_off();

	if (wq != NULL){
		assert(wq -> head == NULL);
		free(wq);
	}

	int k = interrupts_set(j);
	assert(!k);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int j = interrupts_off();

	if (queue == NULL){

		int k = interrupts_set(j);
		assert(!k);

		return THREAD_INVALID;

	} else if (head == NULL){

		int k = interrupts_set(j);
		assert(!k);

		return THREAD_NONE;

	} else {

		struct thread* imp = current_running;
		current_running -> next = NULL;
		struct thread* temp = queue -> head;
		if (queue -> head == NULL)
			queue -> head = current_running;
		else {
			while(temp -> next != NULL)
				temp = temp -> next;
			temp -> next = current_running;
		}

		current_running = head;
		head = head -> next;
		current_running -> next = NULL;
		Tid ret = current_running -> tid;

		setcontext_called = 0;
		getcontext(&(imp -> mycontext));
		clean();
		if (!setcontext_called){
			setcontext_called = 1;
			setcontext(&(current_running -> mycontext));
		}

		int k = interrupts_set(j);
		assert(!k);

		return ret;

	}

	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns the number of threads that were woken up. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int j = interrupts_off();

	if (queue == NULL || queue -> head == NULL){
		
		int k = interrupts_set(j);
		assert(!k);

		return 0;

	} else if (all == 0){

		struct thread* temp = queue -> head;
		queue -> head = queue -> head -> next;
		temp -> next = NULL;

		struct thread* tmp = head;
		if (head == NULL){
			head = temp;
		} else {
			while (tmp -> next != NULL)
				tmp = tmp -> next;
			tmp -> next = temp;
		}

		int k = interrupts_set(j);
		assert(!k);

		return 1;

	} else if (all == 1){

		int count = 0;
		struct thread* temp;
		struct thread* tmp;
		while (queue -> head != NULL){
			temp = queue -> head;
			queue -> head = queue -> head -> next;
			temp -> next = NULL;

			tmp = head;
			if (head == NULL){
				head = temp;
			} else {
				while (tmp -> next != NULL)
					tmp = tmp -> next;
				tmp -> next = temp;
			}
		count++;
		}
		assert(queue -> head == NULL);

		int k = interrupts_set(j);
		assert(!k);

		return count;
	}

	return THREAD_FAILED;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	int j = interrupts_off();

	if (tid < 0 || tid >= THREAD_MAX_THREADS || tid_array[tid] != -1 || tid == current_running -> tid){
		
		int k = interrupts_set(j);
		assert(!k);

		return THREAD_INVALID;

	} else {
		
		struct thread* tmp = tcb_array[tid];
		
		if (tmp -> wq == NULL)
			tmp -> wq = wait_queue_create();

		//Tid ret = current_running -> tid;
		thread_sleep(tmp -> wq);

		//
		int k = interrupts_set(j);
		assert(!k);

		return tid;
	}

	return THREAD_INVALID;
}

struct lock {
	/* ... Fill this in ... */
	struct wait_queue* wq;
	Tid current;
};

struct lock *
lock_create()
{
	int j = interrupts_off();

	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock -> wq = wait_queue_create();
	lock -> current = -1;

	int k = interrupts_set(j);
	assert(!k);

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	int j = interrupts_off();

	assert(lock != NULL);

	assert(lock -> current == -1);
	wait_queue_destroy(lock -> wq);
	free(lock);

	int k = interrupts_set(j);
	assert(!k);
}

void
lock_acquire(struct lock *lock)
{
	int j = interrupts_off();

	assert(lock != NULL);

	while (lock -> current != -1){
		interrupts_set(j);
		thread_sleep(lock -> wq);
		j = interrupts_off();
	}

	lock -> current = current_running -> tid;

	int k = interrupts_set(j);
	assert(!k);
}

void
lock_release(struct lock *lock)
{
	int j = interrupts_off();

	assert(lock != NULL);

	assert (lock -> current == current_running -> tid);
	thread_wakeup(lock -> wq, 1);
	lock -> current = -1;

	int k = interrupts_set(j);
	assert(!k);
}

struct cv {
	/* ... Fill this in ... */
	struct wait_queue* wq;
};

struct cv *
cv_create()
{
	int j = interrupts_off();

	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	cv -> wq = wait_queue_create();
	//lock -> current = -1;

	int k = interrupts_set(j);
	assert(!k);

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int j = interrupts_off();

	assert(cv != NULL);
	
	//check no threads waiting?
	thread_wakeup(cv -> wq, 1);

	wait_queue_destroy(cv -> wq);
	free(cv);

	int k = interrupts_set(j);
	assert(!k);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int j = interrupts_off();

	assert(cv != NULL);
	assert(lock != NULL);

	assert(lock -> current == current_running -> tid);

	//while?
	lock_release(lock);
	thread_sleep(cv -> wq);
	lock_acquire(lock);

	int k = interrupts_set(j);
	assert(!k);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int j = interrupts_off();

	assert(cv != NULL);
	assert(lock != NULL);

	assert(lock -> current == current_running -> tid);

	thread_wakeup(cv -> wq, 0);

	int k = interrupts_set(j);
	assert(!k);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int j = interrupts_off();

	assert(cv != NULL);
	assert(lock != NULL);

	assert(lock -> current == current_running -> tid);

	thread_wakeup(cv -> wq, 1);

	int k = interrupts_set(j);
	assert(!k);
}
