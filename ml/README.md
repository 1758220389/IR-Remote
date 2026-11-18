# MPU6050 / MPU6500 手势采集与识别

当前正式遥控器已启用手势控制并通过 CW 升温、CCW 降温实体测试，见 [整合与验收](analysis/SHADOW.md)。下文独立采集／识别平台用于复现，不能直接用于正式固件；影子模式是历史阶段。

复现顺序：安装 `requirements.txt` → `Start-Capture.cmd` 采集 → `analysis/build_v1.py` 生成清单与特征 → `analysis/train_v1.py` 训练评估 → `analysis/export_recognizer.py` 导出（--check 只核对冻结参数）→ Keil 构建。原始数据及质量／排除记录位于 `data/real`，冻结清单在 `data/v1`，参数与评估在 `models/v1`。详见 [分析](analysis/README.md)、[训练](analysis/TRAINING.md)、[独立部署](analysis/DEPLOYMENT.md)。

冻结数据共 130 个有效样本，主集 120 个。三分类逻辑回归四轮留一会话交叉验证 118/120（98.33%），NONE 误触发 0/40；板端历史重放 128/128 与 Python 一致，另两条截断拒绝。独立识别相对采集固件增量 Flash 2,288 B、RAM 64 B，处理约 125～250 µs，不是正式整合固件的增量或端到端耗时。

本目录提供 STM32 六轴数据采集固件和 Windows 端 Python 工具。在仓库根目录运行下文的 PowerShell 命令。使用 VS Code 时可选用 `ml\.venv\Scripts\python.exe` 作为解释器。

## Windows 图形采集

在资源管理器双击 `ml/Start-Capture.cmd`。程序使用本机已有的 `ml/.venv`、Tk、Matplotlib、pyOCD 和 STM32F1 设备包。保持 ST-Link 连接并供电。先关闭 Keil 调试和其他 SWD 采集命令。同一图形窗口负责连接、波形、保存和确认。

| 标签 | 动作 | 控制映射 |
| --- | --- | --- |
| CW | 自然握持，顺时针旋腕一次 | 升温 |
| CCW | 自然握持，逆时针旋腕一次 | 降温 |
| NONE | 保持静止，或做非目标动作 | 无 |

固定传感器在遥控器内的安装朝向，从同一侧观察手腕来判断顺逆时针。允许水平、自然倾斜和较大仰角三种初始姿态，试采时三种都要覆盖，先比较原始波形和不同速度的影响。此前的 LEFT / RIGHT 等历史标签和目录继续保留。

1. 点击「开始新采集」，软件自动命名。中断后点击「继续上次采集」。依次采 CW 10 个、CCW 10 个、NONE 静置 5 个、NONE 非目标动作 5 个。
2. 看到「可以按 BT6」后，按下开始，完成动作即松开。150 ms 以下取消；到 1.5 秒自动完成并标记达到上限。等「可以按 BT6」重新出现后再按。实时窗显示六轴，右侧可查看完整窗口和质量检查。
3. 质量异常仍保留原始数据。误按或方向错误时，选中样本并点击「排除所选样本」。引导会返回该样本所属的阶段补足有效数量。提示音可随时关闭。
4. 要手动采集，选择标签和目标有效数，需要采静置零偏时勾选「NONE 静置」，再点击「开始 / 更新手动采集」。暂停、会话或阶段切换会等板端处理完当前窗口且 BT6 松开。暂停期间若按键，完成的窗口会保存并排除。
5. 重连先处理旧阶段的待确认窗口，再应用阶段切换请求。关闭程序时保存当前阶段，重新打开可恢复。板子若由 ST-Link 供电，应保持 USB 插着，掉电或复位会清除板端 RAM。首次打开若出现未归属的窗口，可保留原始记录并归档排除，再开始当前会话。

原始 CSV、质量与会话 JSON、排除标记分别保存在 `ml/data/real/sessions/<会话>/<标签>/<窗口>/`。底部的会话和样本下拉框可查看历史数据。v4 采集计数只纳入 v4 窗口，旧版固定窗仍可查看波形及排除记录。图形界面和 CLI 的新标签均为 CW、CCW、NONE。

## 独立采集板接线

下表用于独立采集固件。正式遥控器的 PB6/PB7 已用于红外，本工程避开这些引脚以及 PB0、PB11、PB13、PA7，并保留 SWD。若使用同伴的板卡，先确认 PB12/PB14 已引出且空闲。

| STM32F103C8T6 | 另一端 | 说明 |
| --- | --- | --- |
| PB12 | MPU6500 SCL | 开漏软件 I²C |
| PB14 | MPU6500 SDA | 开漏软件 I²C |
| 3.3V | MPU6500 VCC | 确认模块支持 3.3 V 供电 |
| GND | MPU6500 GND、AD0 | AD0 接低，7 位设备地址为 0x68 |
| PA9 | USB-TTL RXD | STM32 发送 |
| PA10 | USB-TTL TXD | 独立采集板使用 UART 确认时连接 |
| PB15 | BT6 一端 | 另一端接 GND，内部上拉，按下为低 |
| GND | USB-TTL GND、ST-LINK GND | 全部共地 |
| PA13 | ST-LINK SWDIO | SWD 下载 |
| PA14 | ST-LINK SWCLK | SWD 下载 |
| NRST | ST-LINK NRST | 可选，便于复位连接 |
| 3.3V | ST-LINK VTref | 若探针有电压参考输入，按探针标识连接 |

USB-TTL 选用 3.3 V **逻辑电平**，USB-TTL 的 VCC 留空。仅观察波形或仅使用 SWD 时，TXD 留空。正式遥控器的 PA10 已用于 BT5，禁止将 USB-TTL TXD 接到运行正式固件的板上。板卡使用一路电源，区分 ST-LINK 的电压参考输入和供电输出。SCL 与 SDA 各需上拉到 3.3 V。模块已有适当上拉时可直接使用，否则各加约 4.7 kΩ。普通下载后运行时 BOOT0 接低。

远端正式遥控器的 OLED 使用 PB9/SCL、PB8/SDA 软件 I²C。五个按键为 PC13/PC15、PA0/PA2、PA3/PA5、PA15/PB4、PA10/PA12，每对前者输出低电平。独立工程复用 `code/Start` 的启动文件和时钟配置，以及 `code/Library` 的 SPL 3.5.0，沿用原工程的 ARMCC5 工具链。

## 正式遥控器资源与集成预留

独立采集工程复用本地的启动文件、时钟配置及 SPL，正式遥控器代码保持原样。

| 资源 | 正式遥控器当前用途 |
| --- | --- |
| PB6、PB7 | PB6/TIM4_CH1 发射载波，PB7/TIM4_TI2 接收。TIM4 在收发之间切换 |
| TIM2、TIM3、DMA1 | TIM2 用于微秒时间戳，TIM3 用于发射脉冲包络定时，DMA1 通道 1/4 用于红外边沿采集 |
| PB8、PB9 | OLED 软件 I²C。硬件 I²C1 重映射也使用这两个引脚 |
| PB0、PB11 | 已从正式固件的 BT4/BT5 退役，采集工程也避开 |
| PA10 | 正式固件 BT5 的低电平输出，独立采集固件使用 PA10 RX 接收可选 UART 确认 |

独立固件已使用 **PB12/SCL、PB14/SDA** 开漏软件 I²C 和 SysTick，以约 100 Hz 采样。未来集成时可复用这对 GPIO，由主循环使用现有毫秒节拍安排采样，并实测红外活动期间的采样抖动。[STM32F103 引脚表](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)

正式固件的链接脚本 `code/IR_Remote.sct` 将应用限制在 `0x08000000–0x0800BFFF`（48 KiB），`flash_backend.h` 将末尾 16 KiB 划为从 `0x0800C000` 与 `0x0800E000` 开始的两个 8 KiB 配置银行。SRAM 上限为 20 KiB，其中红外 DMA 两个 512 项的 16 位边沿缓冲已占 2 KiB。模型实际预算仍需用搭档相同配置的 Keil 链接 map 核算。Flash 从 48 KiB 应用区扣除现有镜像占用（含 RW 初始化镜像），RAM 从 20 KiB 扣除现有静态数据、栈和堆，并为运行时峰值留余量。窗口若保留六轴 `int16_t` 原始值，每样本占 12 字节，例如 50 个样本占 600 字节，另计窗口元数据、特征及推理工作区。该数字用于估算，窗口长度待采样后确定。

未来分类接口接收一个完整的、按时间升序排列的六轴窗口，沿用下文的轴序和量纲，并附样本数与采样周期。分类结果为 `CW`、`CCW` 或 `NONE`。`NONE` 不派发事件。其余结果由未来适配层在应用循环中映射为现有 `ButtonEvent`（`key` 与 `type`），交给现有按键分发路径。按键映射、允许触发的 UI 页面和重复触发规则由双方确认后再接入。

## 编译与下载

1. 本机已复用 `D:\TOOL\51\Keil5 C51\UV4\UV4.exe` 和 ARM Compiler 5.06 update 5。新电脑使用具备 ARMCC5 的 MDK-Arm 和 STM32F1xx_DFP 2.2.0。[Keil 下载](https://www.keil.com/download/product/) · [STM32F1 设备包](https://www.keil.arm.com/packs/stm32f1xx_dfp-keil/versions/)
2. 在 µVision 打开 `ml/firmware/GestureCapture.uvprojx`，按 F7 编译。链接脚本限制 Flash 从 `0x08000000` 开始，长度上限 48 KiB，RAM 上限 20 KiB。时钟复用原工程面向 8 MHz 外部晶振的配置。
3. 优先使用专用测试板。若借用同伴的板，先用可信的 ST-LINK 读回工具备份完整 64 KiB Flash（`0x08000000` 起），另存后 16 KiB 配置区（`0x0800C000` 起），确认大小及读回一致，并保留恢复用的固件备份。
4. 在本工程的 **Options for Target → Debug** 选 ST-Link Debugger，接口选 SW。在 Flash Download 选适用于 STM32F103C8 的 STM32F10x Medium-density 算法，使用 **Erase Sectors** 和 **Verify**。确认镜像地址低于 `0x0800C000` 后按 F8 下载，使用同伴的板时再读回配置区，与备份逐字节比较。

## 仅用 ST-Link 采集

固件把六轴原始读数和毫秒时间戳写入 RAM 环形缓冲区。电脑通过 SWD 读取，实时绘图和 CSV 字段与串口模式一致。使用同次构建生成的 Keil map 和固件，采集时关闭其他占用 ST-Link 的调试会话。本机虚拟环境已安装 pyOCD：

```powershell
$pack = 'C:\your-keil\ARM\PACK\.Download\Keil.STM32F1xx_DFP.2.2.0.pack'
.\ml\.venv\Scripts\python.exe .\ml\scripts\capture_swd.py --pack $pack --label NONE --live --quiet
```

换电脑时替换 `$pack` 的路径。多只 ST-Link 同时连接时，可用 `--probe` 指定探针 ID。保持板持续运行，复位后重新开启采集。窗口关闭或 Ctrl+C 会保存 CSV。出现缓冲区溢出提示时，检查 CSV 时间戳间隔并重采有缺口的批次。

换用 PB14 后，SCL 和 SDA 均可在释放后回到高电平。本板模块在 I²C 地址 `0x68` 应答，`WHO_AM_I`（寄存器 `0x75`）实读 `0x70`，固件按 MPU6500 初始化。SWD 采集无需连接 PA9 上的 USB-TTL。

实板静置采集记录（2026-11-17）为 `ml/data/real/NONE/20261117T050021Z_e088477b.csv`。通过 ST-Link 连续收到 3000 条，2999 个相邻间隔均为 10 ms，记录时长 29.99 s。加速度向量模的均值为 1.028 g。陀螺仪三轴均值为 6.068、1.531、−1.112 °/s，有稳定的零偏，当前输出保留原始物理量。已完成 MPU6500 实板采集和配置区读回核对。MPU6050 分支已编译，仍需用对应实物复测。

## BT6 触发采集

PB15 的按下和松开均做 20 ms 消抖。按下开始，松开结束。少于 150 ms 的误触取消；达到 1.5 秒上限时只生成一次窗口，记录达到上限并等松开。标称 100 Hz，长度可变且最多 150 条。

完成后，独立 RAM 缓存持有原始值与时间戳、启动内递增编号、传感器 ID、长度、实际时长、截断标记和就绪位。使用本次构建生成的 v4 固件与 map。v4 的 CSV 和 JSON 均保存协议版本、时长、点数与截断标记。电脑把 CSV 与 JSON 一起提交到会话目录，再按编号确认。缓存未确认时 BT6 拒绝新窗口。断开探针或 USB-TTL 时保持 STM32 独立供电，避免复位或掉电清除 RAM。采集工具会尝试重连，沿用同一个会话和标签补读。电脑按编号、传感器 ID 和原始记录的摘要去重，适用于保存后确认失败的重试。每块板同时只运行一个 BT6 采集程序。

优先使用有 3.3 V 逻辑电平的 USB-TTL，连接 PA9/RXD、PA10/TXD 和共地。替换实际 COM 号。串口待确认期间循环发送缓存窗口，收到确认后恢复连续波形。SWD 连续环形缓存持续更新。

```powershell
$py = '.\ml\.venv\Scripts\python.exe'
& $py ml/scripts/capture.py --list
& $py ml/scripts/capture.py --port COM9 --trigger --session pilot01 --label CW --windows 10 --live
```

没有可双向通信的 USB-TTL 时，使用同次构建生成的固件和 map，通过 ST-Link 读取。关闭占用探针的 Keil 调试会话。出现 AP error 时保持供电，确认探针重新连接，必要时以 100 kHz 启动。

```powershell
$py = '.\ml\.venv\Scripts\python.exe'
$pack = 'C:\your-keil\ARM\PACK\.Download\Keil.STM32F1xx_DFP.2.2.0.pack'
& $py ml/scripts/capture_swd.py --pack $pack --swd-hz 1000000 --trigger --session pilot01 --label CW --windows 10 --live
```

## 实板验收与试采集

1. 先使用独立的 `acceptance01` 会话。轻点时间小于 150 ms 应取消。短时旋腕后松手应得到有效短窗；长按超过 1.5 秒应只保存一个并标记达到上限，松开再按应生成下一编号。
2. 单独用 SWD 验证断线恢复。保持板供电，暂拔探针的 USB，长按一次再松开，等待超过 3 秒并再长按一次。插回探针后应只补存第一条窗口。如果板子由 ST-Link 供电，保持探针插着，通过关闭并重开电脑端采集程序验证补读。用 UART 时也可断开 USB-TTL 的电脑端，保持板供电。保存后重开程序，核对没有重复文件。
3. 使用新会话，按 CW、CCW、NONE 各采 10 个通过质量检查的窗口。固定模块的安装方向，动作安排在按住期间，并在继续按键前看一眼保存结果。NONE 可先以 `--label NONE --stationary --windows 5` 采五个静置窗口，再去掉 `--stationary`，以 `--windows 10` 收集五个非目标动作，如轻微抖动、拿起或放下。静置陀螺仪均值写在每个静置窗口的 JSON 中，CSV 保留原始读数。
4. 检查点数与实际时长是否一致、相邻间隔是否为 10 ms，以及各轴是否出现量程端点。质量失败仍保存原始数据，但不计入通过数。各抽查 CW、CCW、NONE 的波形，比较六轴方向、幅度和时序。误按或动作不合格时用 `--reject` 排除，再运行原命令补采。

```powershell
& $py ml/scripts/windows.py '<窗口目录>'
& $py ml/scripts/plot.py '<窗口目录>/raw.csv' --show
& $py ml/scripts/windows.py '<窗口目录>' --reject
```

每个窗口放在 `ml/data/real/sessions/<会话>/<标签>/<窗口>/`。排除操作添加 `rejected` 标记。JSON 记录验收结果、会话、传感器、采样率、量程和来源。Flash 配置银行继续预留；进入训练前先汇总真实试采窗口并评估三类动作差异。新版实板状态见下文进度。

## 查看串口并采集

打开 Windows 设备管理器的「端口（COM 和 LPT）」，插拔 USB-TTL 对照 COM 号，也可以运行：

```powershell
.\ml\.venv\Scripts\python.exe .\ml\scripts\capture.py --list
```

用实际 COM 号替换 COM3，先采一段静止数据：

```powershell
.\ml\.venv\Scripts\python.exe .\ml\scripts\capture.py --port COM3 --baud 115200 --label NONE --live --quiet
```

打开采集后复位 STM32，确认终端出现 `# OK MPU6050 WHO_AM_I=0x68` 或 `# OK MPU6500 WHO_AM_I=0x70`。波形窗口显示最近约 5 秒的数据。串口参数为 115200、8N1、无流控。静止时角速度应接近零，三轴加速度向量的模应约为 1 g。移动板子观察变化，检查相邻时间戳是否通常相差约 10 ms。时间戳来自发起 I²C 读取时的板载毫秒计数。按 Ctrl+C 或关闭波形窗口结束，终端会打印 CSV 路径。去掉 `--quiet` 可逐行显示数据。固定样本数可加 `--count 250`。

每次运行生成独立批次和一个标签。进行不同动作时分别启动，选择 `CW`、`CCW` 或 `NONE`。真实数据保存到 `ml/data/real/<标签>/<批次>.csv`。更换板子、复位或更换动作时新开批次。

- 出现 `WHO_AM_I read failed` 或 `I2C six-axis read failed` 时，检查供电、共地、AD0、PB12/PB14 顺序和上拉。若线路因持续拉低无法恢复，检查电路后重新上电。
- 无输出或乱码时，核对 COM 号、8N1、波特率、PA9 到 RXD 的连接及共地，确保其他串口软件已关闭。若仍异常，核对烧录是否成功和板上晶振。Windows 若未枚举新端口，按 USB-TTL 芯片型号检查驱动。

## 数据约定与离线验证

串口字段为 `t_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps`。时间为 STM32 启动后的毫秒数，三轴方向沿传感器芯片坐标系，无软件换轴。两款芯片均配置 ±2 g（16384 LSB/g，含重力）与 ±250 °/s（131 LSB/(°/s)）。I²C 读取失败输出 `# ERROR`，随后重试初始化。保存时追加 `label,batch_id,source`。采真实动作前固定模块安装朝向并记录。

两款芯片均配置 `CONFIG=0x04`，陀螺仪带宽约 20 Hz，输出基准 1 kHz，`SMPLRT_DIV=9` 降至 100 Hz。MPU6050 的加速度滤波也由 `CONFIG` 控制，带宽约 21 Hz。MPU6500 通过 `GYRO_CONFIG.FCHOICE_B=0` 选用陀螺仪 DLPF，另写 `ACCEL_CONFIG2=0x04` 选择约 20 Hz 的加速度 DLPF，并清除 `PWR_MGMT_2` 的待机位。初始化会读回采样配置；身份错误或配置失败时不发布数据。[MPU6050 寄存器手册](https://www.olimex.com/Products/Modules/Sensors/MOD-MPU6050/resources/RM-MPU-60xxA_rev_4.pdf) · [MPU6500 寄存器手册](https://www.ic-components.jp/files/5b/MPU-6500.pdf)

本机已使用 Python 3.13.5 x64 创建虚拟环境。换电脑时可用 `python -m venv ml/.venv` 重建，再运行 `ml/.venv/Scripts/python.exe -m pip install -r ml/requirements.txt`。以下为已有的开发自检入口。独立窗口 CSV 可直接交给绘图脚本。

```powershell
$py = ".\ml\.venv\Scripts\python.exe"
& $py ml/scripts/check_env.py
& $py ml/scripts/simulate.py --with-errors
& $py ml/scripts/capture.py --synthetic-input ml/data/synthetic/stream.txt --label CW --count 250 --quiet
$sample = Get-ChildItem ml/data/synthetic/CW -Filter *.csv |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
& $py ml/scripts/plot.py $sample.FullName
```

模拟流带有 `# SOURCE=synthetic` 首行，采集结果的 `source` 为 `synthetic`，结果写入 `ml/data/synthetic/`。绘图默认在 CSV 旁保存 PNG。已验证库导入、模拟串口回环、异常行拒绝、250 行 CSV 重读和六条实时曲线的数据更新。真实 COM 接收和动态轴向测试仍待验证。真实批次在 `ml/data/real/`，`ml/models/` 供后续模型阶段使用。

## 进度

- 2026-11-17：完成 BT6 冻结缓存、保存后确认、SWD 重连补读和 UART 帧传输。ARMCC 5.06u5 全量重编译为 0 错误、0 警告。链接占用 Flash 11,492 B / 48 KiB（HEX 最高地址 0x08002CE3），RAM 8,256 B / 20 KiB（含链接所列栈和堆），其中待确认缓存 2,420 B。这些数字来自独立固件。
- 使用已有实板静置记录和临时目录验证 UART 帧校验、SWD 保存后确认前断线的补读去重、原始值写入、排除标记、时间间隔与饱和判定。此前 MPU6500 采样和实体 BT6 已验证，新版协议的实板验收仍待完成。
- 当前实板仅连接 ST-Link。探针参考电压约 3.26 V，但常规连接、100 kHz / 1 MHz 只读重试和复位下连接均未读到 IDCODE。烧录停在目标连接阶段，未擦写 Flash。恢复 SWD 后先备份和按扇区下载，再验收断线补读，最后开始 pilot01。
- 重试后已连接并备份完整 64 KiB 及 16 KiB 配置区至 `ml/data/flash-backups/20261117-171210/`。首次扇区擦除超时，复位后确认配置区与备份一致。以 1 MHz、复位停核、仅扇区擦除重试成功，下载映像和配置区读回均一致。
- 新版实体 BT6 首轮验收：短按取消一次，长按保存 1 个窗口。150 条，149 个间隔均为 10 ms，六轴均未饱和。该静置窗口的陀螺仪三轴均值约为 5.01、0.35、−1.19 °/s。

- 新版 SWD 补读实板验收通过。板由 ST-Link 供电，保持 USB 连接，仅关闭电脑端 SWD 会话。两次实体长按只产生一个待确认窗口，连续环形缓存仍更新。重连补存 150 条，149 个间隔均为 10 ms，六轴均未饱和。保存后确认清除就绪位，再重连未出现重复文件。

- 桌面平台使用实板验证了 ST-Link 连接、MPU6500 识别、100 Hz 配置显示及实时波形。临时会话自检覆盖保存后确认中断、阶段绑定、质量失败、暂停及排除补采。本轮沿用已验收固件。

- v4 修复了回读历史 CSV 时将毫秒时间戳转换为浮点数、导致 Tk 波形刷新回调中断的问题。使用原有 Keil ARMCC 5.06u5（build 528）全量重编译，0 错误、0 警告；Flash 11,664 B，RAM 8,264 B。仅擦除应用区扇区下载，固件和末尾 16 KiB 配置区均已逐字节读回验证。
- v4 实板验收：GUI 连续采集恢复就绪，实测未截断窗口 1426 ms / 143 点、156 ms / 16 点；长按窗口 1500 ms / 150 点，仅生成一次且标记截断。保留 ST-Link 供电，关闭电脑端会话后完成的短时旋腕（698 ms / 70 点）已重连补存。四个窗口的相邻间隔均为 10 ms，六轴无饱和，GUI 有效计数为 4/10。约 75 ms 的 SWD 开漏 PB15 电平测试只增加取消计数，未生成窗口。验收数据与正式试采分开。
## 独立手势识别

双击 ml/Start-Recognition.cmd，查看 BT6 同一窗口的 MCU 输出与 PC 对照。普通上电使用采集模式。脱离电脑时按住 BT6 上电，启动后松开即可进入识别模式。板端推理不等待电脑确认，正式红外遥控器未接入。

模型版本、精度、链接占用、验收状态和复现方法见 [独立识别部署报告](analysis/DEPLOYMENT.md)。

## 当前进度（2026-11-18）

正式控制已完成，见整合报告。上文连接失败、尚未集成及影子阶段描述为历史记录。当前灵敏度仍待后续版本优化，本轮不改模型。完整红外学习、配置持久化整机回归和更大规模实体准确率仍待验证。

设备包路径由 GUI 选择，或设置环境变量 `STM32_DFP_PACK`；仓库不固定本机 Keil 安装路径。
