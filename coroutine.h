#ifndef __COROUTINE_H__
#define __COROUTINE_H__
#include <stdint.h>

// 协程执行的函数定义
typedef void* (*coRunFunc)(void *arg); 

// 协程结构的定义
/**
 * 协程状态说明
 * 未启用：stack_start == NULL and stack_stop == NULL
 * 已执行结束：stack_start == NULL and stack_stop != NULL
 * 激活：stack_start != NULL and stack_stop != NULL
 */
typedef struct _coroutine
{
    char *__stack_start;  // 栈顶
    char *__stack_stop;   // 栈底
    char *__stack_copy;   // 保存栈的位置
    uint64_t __stack_saved;  // 保存的栈空间大小
    struct _coroutine *__parent;       // 父协程
    coRunFunc __run_func;  // 协程执行的函数
} Coroutine;

typedef Coroutine* pcoroutine_t;

// 初始化coroutine
int coroutine_init(coRunFunc main);
// 创建协程, 不指定父协程，默认使用当前协程做父协程
pcoroutine_t coroutine_create(coRunFunc func);
// 创建协程, 指定父协程
pcoroutine_t coroutine_create_parent(pcoroutine_t parent, coRunFunc func);
// 销毁协程 
int coroutine_destroy(pcoroutine_t self);
// 切换协程
void *coroutine_switch(pcoroutine_t target, void *arg);

#define coroutine_destroy_null(self) \
    do { \
        printf("destroy coroutine\n"); \
        coroutine_destroy(self); \
        self = NULL; \
    } while(0)

// 根协程
//#define COROUTINE_ROOT(op)       (((pcoroutine_t)(op))->__stack_stop == (char*) -1)
// 协程已开启
#define COROUTINE_STARTED(op)    (((pcoroutine_t)(op))->__stack_stop != NULL)
// 协和已激活
#define COROUTINE_ACTIVE(op)     (((pcoroutine_t)(op))->__stack_start != NULL)
// 获取父greenlet
#define COROUTINE_GET_PARENT(op) (((pcoroutine_t)(op))->__parent)

#endif  //__COROUTINE_H__

