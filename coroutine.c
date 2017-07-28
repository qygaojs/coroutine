#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "coroutine.h"
#include "coerr.h"

/**
 * 声明静态函数
 */
static int coroutine_start(void *arg, void *mark);
static int coroutine_switch_stack();
static int slp_save_state(char* stackref);
static int coroutine_save(pcoroutine_t g);
static void slp_restore_state();
static int slp_switch();

/**
 * 根协程
 * stack_stop = -1 and stack_start != CO_NULL
 */
pcoroutine_t co_root    = CO_NULL;  // 根协程
pcoroutine_t co_current = CO_NULL;  // 当前工作的协程
pcoroutine_t co_target  = CO_NULL;  // 切换的协程

/**
 * 初始化coroutine
 */
int coroutine_init(coRunFunc main)
{
    char *mark;
    if (co_root) {
        printf("co_root is exists.");
        return CO_OK;
    }
    // 创建co_root,并初始化
    co_root = (pcoroutine_t)malloc(sizeof(Coroutine));
    if (co_root == CO_NULL) {
        printf("malloc failed.");
        return CO_MEM_ERR;
    }
    memset(co_root, 0, sizeof(Coroutine));
    co_root->__stack_start = (char *)1;
    co_root->__stack_stop  = (char *)&mark;
    co_root->__run_func    = main;
    // 创建成功后，co_current -> co_root
    co_current = co_root;
    printf("create co_root success.\n");
    return CO_OK;
}

/**
 * 创建协程
 */
pcoroutine_t coroutine_create(coRunFunc func)
{
    if (!func) {
        printf("func is not exist.");
        return CO_NULL;
    }
    assert(co_root != CO_NULL);    // co_root不能为NULL
    //if (!co_root) {
    //    // root 不存在且创建失败，直接返回CO_NULL
    //    printf("co_root create failed.\n");
    //    return CO_NULL;
    //}
    // 创建新协程
    // 创建后，协程并未启动，所以self->__stack_start = self->__stack_stop = CO_NULL
    // 协程也未切换，所以self->__stack_copy = CO_NULL self->__stack_saved = 0
    pcoroutine_t self = (pcoroutine_t)malloc(sizeof(Coroutine));
    memset(self, 0, sizeof(Coroutine));
    self->__parent = co_current;  // 父协程为当前工作的协程
    self->__run_func = func;
    return self;
}

/**
 * 创建协程并指定父协程
 * 如果parent为CO_NULL，则函数与coroutine_create一样
 */
pcoroutine_t coroutine_create_parent(pcoroutine_t parent, coRunFunc func)
{
    pcoroutine_t self;
    if (!(self = coroutine_create(func))) {
        printf("create new coroutine failed.");
        return CO_NULL;
    }
    if (parent) {
        self->__parent = parent;
    }
    return self;
}

/**
 * 销毁协程 
 * 谁创建，谁销毁
 * XXX:会不会导致内存泄漏呢？
 *     main 函数中创建test1函数的协程 test1函数中创建test2函数的协程，test2函数中创建test3函数的协程
 *     main中切换test1执行，test1切换到test2执行，test2切换到test3中执行，test3切换到test1执行
 *     这时test1执行完成，test1执行完，释放test2协程句柄，返回main函数，main执行完，释放test1句柄，main执行完
 *     这时test3的句柄未释放(当然程序结束必然会释放，只是举个例子,考虑可行行)
 */
int coroutine_destroy(pcoroutine_t self)
{
    if (!self) {
        return CO_OK;
    }
    if (self->__stack_copy) {
        free(self->__stack_copy);
        self->__stack_copy = CO_NULL;
    }
    free(self);
    return CO_OK;
}

void *g_arg = CO_NULL;
/**
 * 切换协程
 */
void *coroutine_switch(pcoroutine_t target, void *arg)
{
    int err = 0;
    //g_arg = args_copy(arg);
    g_arg = arg;
    // 判断target的__run_func是否存在
    coRunFunc run_func = target->__run_func;
    if (run_func == CO_NULL) {
        printf("run_func is NULL.\n");
        return CO_SWITCH_FAILED;
    }
    // 切换协程
    // 如果该协程已结束，则执行其父协程
    while(target) {
        if (COROUTINE_ACTIVE(target)) {
            // 直接切换
            co_target = target;
            err = coroutine_switch_stack();
            break;
        }
        if (!COROUTINE_STARTED(target)) {
            // 如果协程未开始执行，则启动它
            char *mark;
            co_target = target;
            if ((err = coroutine_start(arg, &mark)) == 1) {
                continue;
            }
            break;
        }
        // 协程已执行结束了,执行其父协程
        target = target->__parent;
    }
    if (err < 0) {
        return CO_SWITCH_FAILED;
    }
    if (g_arg)
        return g_arg;
    else
        return arg;
}

/**
 * 协程启动
 * arg:    参数
 * mark:   标记准备切换的函数的栈底
 */
static int coroutine_start(void *arg, void *mark)
{
    void *result;   //函数执行结果
    coRunFunc run_func = co_target->__run_func;
    co_target->__stack_start = CO_NULL;    // 后面会计算栈顶，现在还不知道
    co_target->__stack_stop  = (char *)mark;       // 保存栈底的位置
    // 切换到co_target
    int err = coroutine_switch_stack();
    if (err < 0) {
        // 切换失败
        co_target->__stack_start = CO_NULL;
        co_target->__stack_stop  = CO_NULL;
    } else if (err == 1) {
        // 切换正确
        co_target->__stack_start = (char *)1;   // 协程开始运行
        result = run_func(arg);                 // 执行协程
        co_target->__stack_start = CO_NULL;     // 协程执行完毕
        pcoroutine_t parent;
        for (parent = co_target->__parent; parent != CO_NULL; parent = parent->__parent) {
            result = coroutine_switch(parent, result);  // 切换到父协程
        }
    }
    return err;
}

/**
 * 切换协程的栈
 */
static int coroutine_switch_stack()
{
    int err;
    err = slp_switch();
    if (err < 0) {
        co_target = CO_NULL;
    } else {
        co_current = co_target;
    }
    return err;
}

//----------------------------------------------------------------

//第一个参数是当前栈的顶部的地址，第二个变量用于保存栈之间的偏移
//这里如果要切换到的协程还没有开始执行，那么返回1
#define SLP_SAVE_STATE(stackref, stsizediff)        \
    if (slp_save_state((char*)stackref)) return -1; \
    if (!COROUTINE_ACTIVE(co_target)) return 1;    \
    stsizediff = co_target->__stack_start - (char*)stackref

#define SLP_RESTORE_STATE()                         \
    slp_restore_state()

/**
 * 保存栈的信息，参数是当前栈顶的地址
 * 这里保存的原则就是:
 * 将当前协程的栈数据保存
 * 以下方法可以优化内存
 * 如果切换到的协程是新启动的协程，则栈必然没有被破坏，
 * 那么只需要保存新协程的栈底与当前协程的栈顶之间的栈数据即可。
 * 如果切换到的协程是已启动过的协程，则栈必须要还原，即必须要破坏掉栈
 * 那么需要将当前协程及其上层的协程栈信息都保存起来
 */
static int slp_save_state(char* stackref)
{
    /* must free all the C stack up to target_stop */
    pcoroutine_t owner = co_current; //当前环境所属的协程
    if (owner->__stack_start == CO_NULL) {
        // 这个表示当前协程已经执行完了，那么指向栈层次的上一个协程
        owner = owner->__parent;  /* not saved if dying */
    } else {
        // 设置当前环境所属协程的栈的顶部地址
        owner->__stack_start = stackref;
    }

    if (owner != co_target) {
        // XXX: 可以优化内存，暂不处理
        if (coroutine_save(owner))  // 保存当前协程的栈的信息
            return CO_MEM_ERR;  /* XXX */
    }
    return CO_OK;
}

//将协程的栈的信息保存到堆空间里面去
static int coroutine_save(pcoroutine_t g)
{
    //计算要保存的栈的大小
    uint64_t sz1 = g->__stack_saved;          // 协程已保存的栈的大小
    uint64_t sz2 = g->__stack_stop - g->__stack_start;  // 协程的栈的大小
    if (sz2 > sz1) {
        // 协程的栈的大小已经改变了
        // 保存当前的栈信息，栈是从高地址向低地址漫延
        char *c;
        if (g->__stack_saved == 0) {
            c = (char *)malloc(sz2);
        } else {
            c = (char *)realloc(g->__stack_copy, sz2);
        }
        if (!c) {
            return CO_MEM_ERR;
        }
        memcpy(c + sz1, g->__stack_start + sz1, sz2 - sz1);
        g->__stack_copy = c;  //表示栈的信息保存到了这个地方
        g->__stack_saved = sz2;   //保存了这么大的大小
    }
    return CO_OK;
}

// 恢复切换到的协程的栈切换之前的数据
static void slp_restore_state()
{
    if (co_target->__stack_saved != 0) {
        memcpy(co_target->__stack_start, co_target->__stack_copy, co_target->__stack_saved);
        free(co_target->__stack_copy);
        co_target->__stack_copy = CO_NULL;
        co_target->__stack_saved = 0;
    }
}

// 栈的操作
// 可能会修改的寄存器
#define REGS_TO_SAVE "r12", "r13", "r14", "r15"

// FIXME:有兼容性问题
static int slp_switch()
{
    int err;
    void* rbp;
    void* rbx;
    unsigned int csr;
    unsigned short cw; 
    register long *stackref;    // 保存栈顶地址
    register long stsizediff;   // 保存当前执行的函数的栈顶，栈底与新执行函数的栈顶，栈底的偏移
    __asm__ volatile ("" : : : REGS_TO_SAVE);   // 可能会修改的寄存器
    __asm__ volatile ("fstcw %0" : "=m" (cw));
    __asm__ volatile ("stmxcsr %0" : "=m" (csr));
    //这里先获取以及保存rbp,rbx,rsp三个寄存器的数据
    __asm__ volatile ("movq %%rbp, %0" : "=m" (rbp));
    __asm__ volatile ("movq %%rbx, %0" : "=m" (rbx));
    __asm__ ("movq %%rsp, %0" : "=g" (stackref));       // 保存栈顶地址(rsp)到stackref
    {   
        // 这里会将栈之间的偏移保存到stsizediff变量
        // 这里是宏定义，如果要切换到的协程并没有运行，
        // 那么这里宏定义里面就会提前返回1, 那么coroutine_start里面就可以开始run的执行了
        // 这里可以看到其实栈的保存是最终地址保存到slp_switch这一层的
        SLP_SAVE_STATE(stackref, stsizediff);   // 把栈的信息保存到堆空间里面去
        // 修改rbp以及rsp寄存器值，用于将当前栈切换到新要执行的协程的执行栈
        __asm__ volatile (
            "addq %0, %%rsp\n"
            "addq %0, %%rbp\n"
            :   
            : "r" (stsizediff)
            );  
        SLP_RESTORE_STATE();    // 恢复要执行的协程的栈
        __asm__ volatile ("xorq %%rax, %%rax" : "=a" (err));    // 保存返回值 rax ^ rax == 0 = err
    }   
    // 这里要注意，前后rbp，rbx的值已经改变了，
    // 因为栈的数据已经改变了,虽然代码指令没有变，但是栈已经变了
    // 因为这里已经恢复了切换到的协程的栈的信息，
    // 所以这里恢复的其实是切换到的协程的栈的寄存器的地址
    __asm__ volatile ("movq %0, %%rbx" : : "m" (rbx));
    __asm__ volatile ("movq %0, %%rbp" : : "m" (rbp));
    __asm__ volatile ("ldmxcsr %0" : : "m" (csr));
    __asm__ volatile ("fldcw %0" : : "m" (cw));
    __asm__ volatile ("" : : : REGS_TO_SAVE);
    return err;
}



