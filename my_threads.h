#ifndef MY_THREADS_H
#define MY_THREADS_H

void thread_func(void (*func)(void*));
void create_thread(void (*func)(void*));

#endif