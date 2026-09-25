#ifndef APL_TASKS_H
#define APL_TASKS_H

/*
 * 七个冻结任务入口只承担调度、IPC 与硬件执行上下文；领域状态及安全 authority
 * 保持在对应 FML 模块。除 Protect 的 AFE device 外，其余入口当前不使用 argument。
 */
void APL_TaskProtect(void *argument);
/* 250 ms periodic wake 只提供执行节拍；采样 transaction、identity 与发布均归 FML。 */
void APL_TaskSample(void *argument);
/* 按安全依赖顺序编排 Health、Recovery、State、FET 和 IWDG 服务。 */
void APL_TaskState(void *argument);
/* 消费独占 CC 队列、推进 SOC 积分并服务持久化。 */
void APL_TaskSoc(void *argument);
/* APL 只提供 1 s execution context；CELLBAL 选择、复核与 sole-writer 事务归 FML。 */
void APL_TaskBalance(void *argument);
/* 调度诊断帧发送、硬件重试和有界 UART 输出。 */
void APL_TaskCanTx(void *argument);
/* 消费 CAN 接收队列并把服务请求交给领域 owner。 */
void APL_TaskCanRx(void *argument);

#endif /* APL_TASKS_H */
