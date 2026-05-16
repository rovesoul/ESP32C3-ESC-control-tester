# ESP32-C3 ESC Tester

ESP32-C3 web-based ESC tester for PWM, pulse-width, and DShot brushless motor control.

这个项目用于验证 ESP32-C3 是否可以驱动电调（ESC）。当前版本保留 WiFi 配网和 NVS 保存，移除了 MQTT 示例逻辑，配网后通过网页控制台调试 ESC 输出。

- GitHub: <https://github.com/rovesoul>
- Bilibili: <https://space.bilibili.com/185878223>

## 当前功能

- 首次启动无 WiFi 配置时，开启 `ESP32-Setup` 热点，密码 `12345678`。
- 手机或电脑连接热点后访问 `http://192.168.4.1` 完成 WiFi 配网。
- 配网成功后固件会主动重启一次，这是正常现象；重启后会读取已保存的 WiFi 配置，连接路由器，并启动 ESC 控制网页。
- 网页支持选择输出 GPIO、PWM 频率、油门占空比、脉宽控制，以及 PWM / DShot 输出协议。
- 滑块/输入框会实时应用到当前输出；点击“保存设置”才写入 NVS。刷新网页会读取当前运行中的参数，因此未保存的滑块调整仍会显示；重启后才恢复到上次保存的 NVS 参数。
- 输出解锁状态不保存，重启后默认未解锁。
- 支持 PWM / Servo PWM 和 DShot150/300/600/1200。DShot 通过 RMT 连续发送数字油门帧。
- 脉宽控制模式仍使用普通 PWM 输出，但网页滑杆直接设置高电平脉宽，固件按当前频率自动换算占空比。
- 切换 PWM / 脉宽控制 / DShot 协议时，网页会自动执行 STOP，停止输出，并把油门滑杆回到最左侧。
- 网页内置 `favicon.svg` 作为浏览器标签图标和页头 logo，并提供 GitHub / Bilibili 入口链接。
- 长按 Boot 按键，也就是 `GPIO9`，5 秒会清除 WiFi 配置并重启进入配网模式。

## PWM 默认值

- 默认 GPIO：`GPIO3`
- 频率范围：`50Hz` 到 `1000Hz`
- 默认频率：`50Hz`
- 默认占空比：`5.0%`
- 默认脉宽：`1000us`
- 默认输出状态：未解锁，输出 `0%`

DShot 模式下，网页的占空比会映射为 DShot 油门值：`0%` 为停机值 `0`，大于 `0%` 时映射到 `48-2047`。

脉宽控制模式下，建议按 ESC 舵机信号习惯直接调：

- 自检/最低油门：`1000us`
- 启动附近：`1050-1100us`
- 中位/中速参考：`1500us`
- 满油门参考：`2000us`

同一个脉宽在不同频率下会对应不同占空比。固件会保存目标脉宽，并在输出时按 `占空比 = 脉宽 / 周期` 自动换算。

在 `50Hz` 下：

- `5.0%` 对应约 `1000us`
- `7.5%` 对应约 `1500us`
- `10.0%` 对应约 `2000us`

## GPIO 建议

ESP32-C3 常见可用数字脚包括 `GPIO0-10`、`GPIO18-21`。本项目网页允许选择：

`GPIO0/1/2/3/4/5/6/7/10/18/19/20/21`

结合 XIAO ESP32-C3 引脚图，建议优先试 `GPIO3` 或 `GPIO4`。其次可以用 `GPIO5/6/7/10`。

注意：

- `GPIO2` 是 A2，也能输出 PWM；但它是 ESP32-C3 启动相关的特殊脚之一，接 ESC 时不如 `GPIO3/4` 安心。
- `GPIO8` 是板载 LED/SDA，当前也用于 WiFi 连接后的呼吸灯。
- `GPIO9` 是 SCL/Boot，当前用于长按清除 WiFi 配置。
- `GPIO20/21` 是串口 RX/TX，调试日志可能会占用。

## 安全提醒

测试 ESC 前先拆桨。点击“解锁输出”后才会按当前参数输出 PWM。STOP 会立即停止输出并把油门归零。

ESC 信号线接所选 GPIO，ESC 信号 GND 必须与 ESP32-C3 GND 共地。电机主电源由 ESC/电源系统供给，不要从 ESP32-C3 给电机供电。

调速时可以先点击“解锁输出”，再拖动占空比滑块。滑块拖动不会频繁写 flash，只有点击“保存设置”才会把当前协议、GPIO、频率、占空比保存到 NVS。

切换协议会自动 STOP。重新选择协议后，需要确认滑杆位置，再点击“解锁输出”。

## 构建

```bash
source /Users/donghuibiao/ESPIDF/idfv6/v6.0/esp-idf/export.sh
IDF_COMPONENT_MANAGER=0 idf.py set-target esp32c3
IDF_COMPONENT_MANAGER=0 idf.py build
```

当前项目不依赖外部托管组件。这里显式关闭组件管理器，是为了避开本机 ESP-IDF Python 环境里 `pydantic_core` 架构不匹配的问题。

```bash
IDF_COMPONENT_MANAGER=0 idf.py -p PORT flash monitor
```
