#include <stdio.h>
#include <stdlib.h>

int fibonacci(int n){
    if (n == 1 || n == 2) return 1;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main(int argc, char *argv[]){
    int n=atoi(argv[1]);
    printf("%d\n", fibonacci(n));
    return 0;
}