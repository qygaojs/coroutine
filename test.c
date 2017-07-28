#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coroutine.h"
#include "coerr.h"


pcoroutine_t t1;
pcoroutine_t t2;

//----------------------------------------------------------------
void *test1(void *args)
{
    printf("before test1\n");
    printf("args:%s\n", (char *)args);
    char *res = coroutine_switch(t2, "test1 switch test2");
    printf("test1 switch test2 result:%s\n", res);
    printf("after test1\n");
    return "test1 OK";
}

void *test2(void *args)
{
    printf("before test2\n");
    printf("args:%s\n", (char *)args);
    char *res = coroutine_switch(t1, "test2 switch test1");
    printf("test2 switch test1 result:%s\n", res);
    printf("after test2\n");
    return "test2 OK";
}

void *test(void *args)
{
    int ret;
    if ((ret = coroutine_init(test)) != CO_OK) {
        printf("coroutine_init failed:%d\n", ret);
        return NULL;
    }
    t1 = coroutine_create(test1);
    t2 = coroutine_create(test2);
    char *res = (char *)coroutine_switch(t1, "test switch test1");
    printf("test switch test1 result:%s\n", res);
    coroutine_destroy_null(t1);
    coroutine_destroy_null(t2);
    return "OK";
}

int main()
{
    //t1 = coroutine_create(test1);
    //t2 = coroutine_create(test2);
    //char *res = (char *)coroutine_switch(t1, "switch test1");
    //printf("result:%d\n", *res);
    //coroutine_destroy_null(t1);
    //coroutine_destroy_null(t2);
    //return 0;
    char *ret = (char *)test(NULL);
    if (ret)
        printf("test coroutine:%s\n", ret);
    else
        printf("test coroutine failed.\n");
    return 0;
}

