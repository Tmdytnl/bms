# BMS V1 Hardware Validation Matrix

状态：BMS V1 Release Baseline 硬件接口验证参考矩阵。

本矩阵把 production-C、静态约束和 target build 证据映射到板级接口的观测维度、仪器与记录要求。`INTERFACE VERIFICATION ITEM` 表示该行是可复现的工程检查项，不是项目状态或开放 blocker。

| ID | Function | Software evidence | Interface observation | Instrument | Record role |
|---|---|---|---|---|---|
| HW-001 | MCU power/reset/clock | `BSP_Clock_Verify`; 72 MHz compile/build configuration | rail/current、reset、HSE/PLL/source/divider 多次 power cycle | DMM, scope, SWD | INTERFACE VERIFICATION ITEM |
| HW-002 | I2C electrical levels | `soft_i2c.c` open-drain operations | PB8/PB9 idle/high/low、电平、pull-up、rise/fall、无 drive-high conflict | scope/logic analyzer | INTERFACE VERIFICATION ITEM |
| HW-003 | I2C frequency/timing | 5 µs half-cycle，timeout tests | START/STOP、SCL high/low、setup/hold、clock stretch、约100 kHz across PVT | scope/logic analyzer | INTERFACE VERIFICATION ITEM |
| HW-004 | I2C stuck-bus recovery | Phase 2 software recovery tests | SDA stuck-low fixture、9 clocks、STOP、成功/失败边界 | logic analyzer | INTERFACE VERIFICATION ITEM |
| HW-005 | AFE wake/communication | AFE startup + BQ CRC transport regressions | PA8 wake、10 ms settle、ACK/CRC、repeated power-cycle/probe | scope, logic analyzer | INTERFACE VERIFICATION ITEM |
| HW-006 | AFE calibration registers | runtime gain/offset decode tests | actual ADCGAIN1/2/ADCOFFSET values、repeatability、temperature dependence | SWD/UART, chamber optional | INTERFACE VERIFICATION ITEM |
| HW-007 | 13S channel mapping | Phase 4 13S mapping tests | one-channel-at-a-time stimulus confirms VC1..8/10..13/15 mapping | cell simulator, DMM | INTERFACE VERIFICATION ITEM |
| HW-008 | Cell voltage accuracy | integer conversion + coherent publication tests | per-cell multi-point accuracy、offset/gain、repeatability/PVT | precision DMM, cell simulator | INTERFACE VERIFICATION ITEM |
| HW-009 | Pack voltage/BAT cross-check | cell-sum authoritative + BAT diagnostic | BAT vs cell-sum vs reference across range | DMM, cell simulator | INTERFACE VERIFICATION ITEM |
| HW-010 | Current calibration/Rsense | CC decode/current conversion tests；SIM 4 mΩ | real Rsense/Kelvin identity、zero offset、bidirectional multi-point accuracy、thermal drift | precision shunt/DMM, source/load | INTERFACE VERIFICATION ITEM |
| HW-011 | Current polarity | software policy `+charge/-discharge` | controlled charge/discharge proves sign on actual board | current probe/source/load | INTERFACE VERIFICATION ITEM |
| HW-012 | NTC accuracy | monotonic table/interpolation tests；SIM_NTC_10K_B3950 | actual part/BOM、temperature sweep、bias/REGOUT/tolerance、error/domain | chamber, reference thermometer, DMM | INTERFACE VERIFICATION ITEM |
| HW-013 | CC_READY | Protect queue/mailbox/W1C production-C tests | event cadence、ALERT、CC read identity、queue full、新est preservation | scope/logic analyzer, load | INTERFACE VERIFICATION ITEM |
| HW-014 | ALERT electrical path | EXTI/task drain/retry simulator tests | PB1 level/edge、polarity、pulse/hold、simultaneous bits、I2C contention | scope/logic analyzer | INTERFACE VERIFICATION ITEM |
| HW-015 | XREADY event | 10-phase recovery + race tests | real event generation、automatic BQ CHG/DSG clear、generation、full recovery | scope, logic analyzer, SWD/UART | INTERFACE VERIFICATION ITEM |
| HW-016 | SYS_STAT W1C physical behavior | Protect sole-owner/static checks；ambiguity model | ACK/STOP commit point、coalesced events、observed-low retirement、no blind replay | logic analyzer, fault fixture | INTERFACE VERIFICATION ITEM |
| HW-017 | HW OV threshold/delay | 4250 mV / 2 s SIM encoding/regression | measured cell trip distribution、delay、ALERT/SYS_STAT、directional response | cell simulator, scope | INTERFACE VERIFICATION ITEM |
| HW-018 | HW UV threshold/delay | 2800 mV / 4 s SIM encoding/regression | measured trip/delay/ALERT/SYS_STAT/directional response | cell simulator, scope | INTERFACE VERIFICATION ITEM |
| HW-019 | HW OCD threshold/delay | SIM selected 42 mV ≈10.5 A / 320 ms | real Rsense current trip/delay、repeatability、HW recovery handshake | source/load, current probe, scope | INTERFACE VERIFICATION ITEM |
| HW-020 | HW SCD threshold/delay | SIM selected 89 mV ≈22.25 A / 200 µs | safe high-current fixture trip/delay、latched behavior、service reset | pulsed source/load, current probe, scope | INTERFACE VERIFICATION ITEM |
| HW-021 | CHG MOS control | FET Manager transaction/readback/quarantine tests | gate voltage、physical conduction/off leakage、fault-direction response | differential scope, source/load | INTERFACE VERIFICATION ITEM |
| HW-022 | DSG MOS control | same register-level evidence | gate voltage、physical conduction/off leakage、fault-direction response | differential scope, electronic load | INTERFACE VERIFICATION ITEM |
| HW-023 | FET switching timing | manager revision/readback evidence | request->register->gate->current timing、rise/fall、overshoot、dead time/topology effects | scope, current probe | INTERFACE VERIFICATION ITEM |
| HW-024 | FET failure/ambiguity | enable ambiguity quarantine simulator tests | bus interruption/readback mismatch fixture，实际 MOS outcome correlation | logic analyzer, scope | INTERFACE VERIFICATION ITEM |
| HW-025 | Cell balancing | pure selection + CELLBAL readback tests | per-channel mapping、balance current、最多2且不相邻、thermal impact、all-off response | DMM/current probe/thermal camera | INTERFACE VERIFICATION ITEM |
| HW-026 | bxCAN peripheral/pins | ARMCC5 target binding；timing/filter static verifier | PA11/PA12 signal、500 kbit/s/87.5%、filter/FIFO/ISR | CAN analyzer, scope | INTERFACE VERIFICATION ITEM |
| HW-027 | CAN transceiver/bus | protocol scenarios | transceiver supply/STB、termination、dominant/recessive voltage、ACK、load | CAN analyzer, differential scope | INTERFACE VERIFICATION ITEM |
| HW-028 | CAN bus-off/burst | software recovery/drop counters | error injection、bus-off recovery、RX burst/FIFO overrun、no safety coupling | CAN fault injector/analyzer | INTERFACE VERIFICATION ITEM |
| HW-029 | USART1 | ARMCC5 binding + read-only telemetry verifier | PA9/PA10 115200 8N1 waveform、long log、partial-write behavior | USB-UART, scope | INTERFACE VERIFICATION ITEM |
| HW-030 | IWDG LSI/arming | health scenarios；State sole-feeder verifier | actual LSI、timeout across PVT、first-advance arm、each-task stall reset | scope, SWD, chamber optional | INTERFACE VERIFICATION ITEM |
| HW-031 | Reset cause/recovery | software no-feed behavior | IWDG reset flag、outputs during reset/startup、repeat boot | SWD, scope | INTERFACE VERIFICATION ITEM |
| HW-032 | Flash address isolation | compile assertions + IROM/map boundary | silicon part/Flash size、read/write only A/B pages、application intact | SWD/programmer | INTERFACE VERIFICATION ITEM |
| HW-033 | Flash erase/program timing | target adapter builds | page erase、halfword program、interrupt/CAN/ALERT/IWDG latency impact | scope, SWD, CAN analyzer | INTERFACE VERIFICATION ITEM |
| HW-034 | Flash brownout | commit-last/power-cut simulator tests | power cuts at erase/body/commit/post-commit；boot selection | programmable supply/switch, logger | INTERFACE VERIFICATION ITEM |
| HW-035 | Flash endurance/retention | 512 randomized simulation transactions | approved cycle plan、wear/retention sampling、failure semantics | power automation, programmer | INTERFACE VERIFICATION ITEM |
| HW-036 | Power integrity | no software substitute | MCU/AFE/transceiver rails、startup/inrush、load/fault transients、brownout margin | scope, current probe | INTERFACE VERIFICATION ITEM |
| HW-037 | Thermal | balance/current policies only | MOS/Rsense/balance resistor/AFE/MCU temperatures across load/ambient | thermocouples/thermal camera/chamber | INTERFACE VERIFICATION ITEM |
| HW-038 | EMI/EMC | no software claim | applicable conducted/radiated immunity/emission plan与结果 | accredited/pre-compliance equipment | INTERFACE VERIFICATION ITEM |
| HW-039 | ESD/EFT | no software claim | test level、coupling、reset/fault/recovery behavior、damage inspection | ESD/EFT equipment | INTERFACE VERIFICATION ITEM |
| HW-040 | Integrated long run | 32 scenarios, races, 50k stress software evidence | correlated voltage/current/temp/CAN/UART/FET interface logs under long run | full bench/logger | INTERFACE VERIFICATION ITEM |

## 记录规则

每行记录至少绑定 board/schematic revision、firmware commit、policy artifact identity、instrument/校准信息、步骤与 acceptance criterion、raw log/waveform、reviewer。不同测试方法分别保留其 evidence identity，不能互相覆盖原始结论。
