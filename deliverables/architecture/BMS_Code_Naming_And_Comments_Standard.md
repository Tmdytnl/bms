# BMS V1 命名与定义处注释规范

状态：已采纳；适用于本项目自有的 `firmware/BSP/`、`DRV/`、`FML/`、`APL/`、`Config/`、`User/` 生产代码。第三方库、历史交付物不批量改写。与现有[可读性与可维护性指南](../../docs/architecture/BMS_Code_Readability_Maintainability_Guide.md)共同使用；本规范细化定义处注释和命名验收。

## 目标

阅读者跳转到变量、成员或函数的定义时，就能判断其用途、归属和关键约束；阅读调用点时，名称能表达动作与对象。注释补充名称不能表达的单位、有效条件、生命周期、副作用和失败语义。不得用注释掩盖含糊命名，也不得为满足覆盖率写重复代码语法的说明。

## 命名

| 对象 | 规则 |
|---|---|
| 对外函数 | 沿用层或模块前缀，例如 `BMS_Protect_*`、`APL_Rtos_*`、`BSP_Flash_*`、`BQ76940_*`；动词表达实际动作。`GetSnapshot` 只读复制，`Submit` 只提交请求，`Service` 有界推进。 |
| 文件内函数 | 保留模块前缀及 `static`；名称表明单一阶段或判断，不用 `HandleThing`、`DoStep1`。 |
| 类型与枚举 | 沿用 `BMS_*_t` 等既有形式；枚举值体现状态或原因，不用缺乏领域含义的数字代号。 |
| 文件内状态 | 沿用 `s_` 前缀。名称说明持有的事实，若有 `pending`、`valid`、`confirmed`、`latched`，须符合实际状态转换。 |
| 局部变量和参数 | 使用完整、可区分的对象名；`now_ms`、`current_ma` 等已有单位的量保留单位后缀。`sequence`、`generation`、`revision`、`request_id` 不混称。短循环索引只有在所属集合一眼可见时才可简写。 |
| 常量和宏 | 沿用大写下划线形式；名称包含所属模块与单位或硬件含义。 |

更名只针对含糊、误导或与实际单位/归属不符的标识符。公共 API 更名时同步调用点、Keil 工程相关引用、测试与静态门；不为表面一致批量更名。

## 定义处注释

1. 每个项目自有的全局或文件静态变量，在定义处有邻近注释，至少解释用途；共享状态还说明写者、读者或生命周期。避免可变全局暴露给不相关模块。
2. 每个结构体和联合体成员、领域枚举值，在声明处有邻近说明。字段涉及物理量时写单位；涉及证据时说明 identity、有效条件或版本；位图说明位义。
3. 名称仍不能独立表达用途、单位、来源或生命周期的函数参数与局部变量，在声明处加说明。`cell_index` 这类所遍历对象清楚的局部变量不强制写“电芯索引”式重复注释。
4. 公共函数在头文件声明前说明功能、输入与输出、返回或失败语义、可见副作用及调用约束。实现文件中在定义前再用简短注释说明该实现的职责或关键阶段；不复制整段接口契约。
5. 私有函数在定义前说明功能与该模块内的作用。若函数名称和单行注释仍不能让读者理解边界，应先检查职责与命名。
6. 对 `W1C`、FET enable、CELLBAL、IWDG、Flash commit、跨任务共享快照等安全相关代码，注释还要说明唯一写者、锁或 identity 的边界、提交点及失败后保留的状态。
7. 注释使用简明中文，代码标识符、寄存器名与单位保持原样。修改代码时同步修改注释；不得把 Simulator 结果描述为硬件实测，也不得把寄存器 readback 描述为外部 MOS 的物理状态。

定义处注释优先于文件顶部的大段说明。允许一条邻近注释覆盖一组确实共享同一用途的相邻声明，但每个成员仍须能单独读懂。注释说明“是什么、为什么、何时有效”，不逐行复述赋值、判断或自增。

### 示例

```c
/* 当前 AFE 生命周期的校准身份；仅 Sample owner 在配置交接时更新。 */
static uint32_t s_calibration_xready_generation;

typedef struct
{
    uint32_t sample_sequence; /* 已完整发布的测量帧序号，不代表 AFE 生命周期。 */
    uint32_t afe_generation;  /* 帧所属的 XREADY 世代；跨代证据不可复用。 */
} BMS_ExampleIdentity_t;

/*
 * 尝试发布一帧完整测量数据。成功时推进 sample_sequence；输入无效或锁忙时
 * 返回 false，并保持原快照不变。仅 Sample 的任务上下文调用。
 */
bool BMS_Data_PublishMeasurement(const BMS_MeasurementFrame_t *frame);

/* 只检查本次 staging 帧的内部一致性，不读取硬件或修改共享快照。 */
static bool BMS_Data_FrameIsValid(const BMS_MeasurementFrame_t *frame)
{
    /* ... */
}
```

示例展示格式，不替代对应生产接口的完整契约。

## 审查清单

- 跳转到定义后，能否无需阅读整个文件就说出变量/成员的用途、单位与有效条件？
- 在函数定义前，能否看出它对哪个阶段负责？公共接口能否看出失败后是否修改输出或状态？
- 名称能否区分 request、effective command、readback，以及 sample sequence、AFE generation、publication revision？
- 注释是否与当前控制流、唯一写者、锁范围和硬件证据一致？
- 修改后的代码是否仍通过架构门、相关场景、竞态、压力与目标构建？

客观项目约束继续由 `firmware/Tests/verify_architecture.py` 检查；注释是否准确、命名是否足够清楚由代码审查和场景走查判断，不用“注释行数”充当质量指标。
