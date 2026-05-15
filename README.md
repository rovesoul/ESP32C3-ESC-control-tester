# ESP32-C3 ESC Tester

这个项目用于验证 ESP32-C3 是否可以驱动电调（ESC）。当前版本保留 WiFi 配网和 NVS 保存，移除了 MQTT 示例逻辑，配网后通过网页控制台调试 ESC 输出。

## 当前功能

- 首次启动无 WiFi 配置时，开启 `ESP32-Setup` 热点，密码 `12345678`。
- 手机或电脑连接热点后访问 `http://192.168.4.1` 完成 WiFi 配网。
- 配网成功后设备重启，连接路由器，并启动 ESC 控制网页。
- 网页支持选择输出 GPIO、PWM 频率、PWM 占空比。
- 滑块/输入框会实时应用到 PWM 输出；点击“保存设置”才写入 NVS。
- 输出解锁状态不保存，重启后默认锁定。
- 当前只实现 PWM / Servo PWM，DShot150/300/600/1200 作为后续功能预留。
- 长按 Boot 按键，也就是 `GPIO9`，5 秒会清除 WiFi 配置并重启进入配网模式。

## PWM 默认值

- 默认 GPIO：`GPIO3`
- 频率范围：`50Hz` 到 `1000Hz`
- 默认频率：`50Hz`
- 默认占空比：`5.0%`
- 默认输出状态：锁定，输出 `0%`

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

测试 ESC 前先拆桨。网页默认锁定输出，点击“解锁输出”后才会按当前参数输出 PWM。STOP 会立即锁定输出。

ESC 信号线接所选 GPIO，ESC 信号 GND 必须与 ESP32-C3 GND 共地。电机主电源由 ESC/电源系统供给，不要从 ESP32-C3 给电机供电。

调速时可以先点击“解锁输出”，再拖动占空比滑块。滑块拖动不会频繁写 flash，只有点击“保存设置”才会把当前 GPIO、频率、占空比保存到 NVS。

## 构建

```bash
source /Users/donghuibiao/ESPIDF/idfv6/v6.0/esp-idf/export.sh
IDF_COMPONENT_MANAGER=0 idf.py build
```

当前项目不依赖外部托管组件。这里显式关闭组件管理器，是为了避开本机 ESP-IDF Python 环境里 `pydantic_core` 架构不匹配的问题。

```bash
IDF_COMPONENT_MANAGER=0 idf.py -p PORT flash monitor
```
