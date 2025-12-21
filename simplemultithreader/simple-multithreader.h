#include <iostream>
#include <list>
#include <functional>
#include <stdlib.h>
#include <cstring>
#include <pthread.h>
#include <time.h>
using namespace std;

int user_main(int argc, char **argv);

void demonstration(function<void()> && lambda) {
    lambda();
}

// 1D info
typedef struct{
    int low, high;
    function<void(int)> func;
}thread_arg1;

// 2D info
typedef struct{
    int low1, high1, low2, high2;
    function<void(int, int)> func;
}thread_arg2;

// 1D caller
void *caller1D(void *ptr){
    thread_arg1 *a = (thread_arg1*)ptr;
    for (int i = a->low ; i < a->high ; i++){
        a->func(i);
    }
    delete a;
    return NULL;
}

// 2D caller
void *caller2D(void *ptr){
    thread_arg2* a = (thread_arg2*)ptr;
    for (int i = a->low1 ; i < a->high1 ; i++){
        for (int j = a->low2; j < a->high2 ; j++){
            a->func(i, j);
        }
    }
    delete a;
    return NULL;
}

// 1D parallel_for(low, high, lambda, nthreads)

void parallel_for(int low, int high, function<void(int)> &&lambda, int numThread){
    clock_t cpu_start, cpu_end;
    time_t irl_start, irl_end;
    int range = high - low;
    if (range <= 0 || numThread <= 0) return;
    numThread = (numThread > range ? range : numThread);

    pthread_t *tid = new pthread_t[numThread];
    cpu_start = clock();
    irl_start = time(NULL);
    int chunk = range / numThread;
    int rem = range % numThread;

    int cur = low;

    for (int i = 0; i < numThread ; i++){
        thread_arg1 *arg = new thread_arg1;
        arg->low = cur;
        arg->high = cur + chunk + (i < rem ? 1 : 0);
        arg->func = lambda;
        cur = arg->high;
        int flag = pthread_create(&tid[i], NULL, caller1D, arg);
        if (flag){
            printf("Thread creation failed\n");
            exit(1);
        }
    }

    for (int i = 0 ; i < numThread ; i++){
        pthread_join(tid[i], NULL);
    }
    irl_end = time(NULL);
    cpu_end = clock();

    printf("1D parallel_for (%d threads) took: %lds IRL and %fs CPU\n", numThread, irl_end - irl_start, (double)(cpu_end - cpu_start) / CLOCKS_PER_SEC);
    
    delete[] tid;
}

// 2D parallel_for(low1, high1, low2, high2, lambda, nthreads)

void parallel_for(int low1, int high1, int low2, int high2, function<void(int,int)> &&lambda, int numThread){
    clock_t cpu_start, cpu_end;
    time_t irl_start, irl_end;
    int range = high1 - low1;
    if (range <= 0 || numThread <= 0) return;
    numThread = (numThread > range ? range : numThread);

    pthread_t *tid = new pthread_t[numThread];

    cpu_start = clock();
    irl_start = time(NULL);

    int chunk = range / numThread;
    int rem = range % numThread;

    int curr = low1;

    for (int i = 0 ; i < numThread ; i++){
        thread_arg2 *arg = new thread_arg2;
        arg->low1 = curr;
        arg->high1 = curr + chunk + (i < rem ? 1 : 0);
        arg->low2 = low2;
        arg->high2 = high2;
        arg->func = lambda;
        curr = arg->high1;
        int flag = pthread_create(&tid[i], NULL, caller2D, arg);
        if (flag){
            printf("Thread creation failed\n");
            exit(1);
        }
    }

    for (int i = 0 ; i < numThread ; i++){
        pthread_join(tid[i], NULL);
    }
    irl_end = time(NULL);
    cpu_end = clock();

    printf("2D parallel_for (%d threads) took: %lds IRL and %fs CPU\n", numThread, irl_end - irl_start, (double)(cpu_end - cpu_start) / CLOCKS_PER_SEC);

    delete[] tid;
}


int main(int argc, char **argv) {
    cout<<"====== Welcome to Simple Multithreader ======\n";
    int rc = user_main(argc, argv);
    cout<<"====== Bye - Bye ======\n";
    return rc;
}

#define main user_main