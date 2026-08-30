#ifndef APL_TASKS_H
#define APL_TASKS_H

/*
 * 七个冻结任务入口只承担调度、IPC 与硬件执行上下文；领域状态及安全 authority
 * 保持在对应 FML 模块。除 Protect 的 AFE device 外，其余入口当前不使用 argument。
 */
void APL_TaskProtect(void *argument);
void APL_TaskSample(void *argument);
void APL_TaskState(void *argument);
void APL_TaskSoc(void *argument);
void APL_TaskBalance(void *argument);
void APL_TaskCanTx(void *argument);
void APL_TaskCanRx(void *argument);

#endif /* APL_TASKS_H */
