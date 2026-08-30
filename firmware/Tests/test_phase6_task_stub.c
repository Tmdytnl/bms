#include "Task/apl_tasks.h"

#define DEFINE_TASK_STUB(name_) \
    void name_(void *argument)  \
    {                           \
        (void)argument;         \
        for (;;) { }            \
    }

DEFINE_TASK_STUB(APL_TaskProtect)
DEFINE_TASK_STUB(APL_TaskSample)
DEFINE_TASK_STUB(APL_TaskState)
DEFINE_TASK_STUB(APL_TaskSoc)
DEFINE_TASK_STUB(APL_TaskBalance)
DEFINE_TASK_STUB(APL_TaskCanTx)
DEFINE_TASK_STUB(APL_TaskCanRx)
