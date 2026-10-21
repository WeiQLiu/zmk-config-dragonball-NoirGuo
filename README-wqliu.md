# Charybdis dongle 分支改动说明（wqliu）

分支：`charybdis-dongle-studio-wqliu`
对照分支：`zmk-for-charybdis` 的 `main-20250226`（无 dongle，手感基准）

---

## 1. 轨迹球手感向无 dongle 版本对齐

无 dongle 版本的轨迹球参数（`config/boards/shields/charybdis/charybdis_right.conf`）：

```conf
CONFIG_PMW3610_CPI=1200
CONFIG_PMW3610_CPI_DIVIDOR=4
CONFIG_PMW3610_POLLING_RATE_125_SW=y
CONFIG_PMW3610_SCROLL_TICK=70
CONFIG_PMW3610_AUTOMOUSE_TIMEOUT_MS=100   # 当前 main 分支；旧工作副本里是默认 400
```

即：**传感器按 1200 CPI 采样，再除以 4，最终 300 CPI、125Hz 的节奏上报**。

dongle 版本原来是 `cpi = <400>`，既没有高分辨率采样，也没有固定 125Hz 节奏，所以主观上会觉得
"没有那边丝滑"。本次做了三处对齐：

| 位置 | 改动 | 作用 |
| --- | --- | --- |
| `boards/shields/charybdis/trackball.dtsi` | `cpi = <400>` → `<1200>` | 传感器高分辨率采样，慢速移动时每帧也有计数 |
| `config/charybdis.keymap` 的 `&trackball_listener` | 增加 `<&zip_xy_scaler 1 4>` | 1200 / 4 = **300 CPI**，与无 dongle 的输出一致，余数会被累计 |
| `split_input_common.dtsi` 的 `fast` / `sniper` / `trackball_listener2` | 补上对应的 `1/4` 缩放 | cpi 变成 1200 后，这些图层仍保持原来的相对倍率（fast 2x、sniper 1/3） |
| `config/charybdis.keymap` 的 `scroll` | 增加 `<&zip_xy_scaler 1 3>` | 滚动链不经过 1/4，单独把 1200 还原成 400，滚动速度保持换 dongle 前不变 |

想微调速度只改 `zip_xy_scaler` 的分母即可：

- `1 3` → 400 CPI（比原来快一点）
- `1 2` → 600 CPI（明显更快）
- `1 4` → 300 CPI（当前，和无 dongle 一致）

> 改基础分母时，`fast` / `sniper` 里的 `1/4` 也要同步改，否则这两个层的相对速度会变。

> dongle 的 split BLE 连接参数是 7.5ms（`CONFIG_ZMK_SPLIT_BLE_PREF_INT` 默认 6），
> 已经比 125Hz 更快，所以这次没有再限制传感器上报间隔。
> 如果以后觉得 BLE 上包太密，可以在左右半的 `.conf` 里加
> `CONFIG_PMW3610_REPORT_INTERVAL_MIN=8`；但它会在运动间歇丢弃未上报的累计位移，
> 低速度起步可能略微发黏，建议先不加。

---

## 2. 鼠标层增加位移门限

原来的写法是：

```dts
input-processors = <&zip_xy_transform (...)>,
                   <&zip_temp_layer 3 600>;
```

`zip_temp_layer` 只要收到**任意一个**移动事件就会切层，所以手指碰一下轨迹球、按键触底震动都会
误进 mouse 层，接着打字就会打错。

本次换成仓库内自带的新输入处理器 `zmk,input-processor-mouse-layer-gate`
（`src/input_processor_mouse_layer_gate.c`），用法：

```dts
mouse_layer_gate: mouse_layer_gate {
    compatible = "zmk,input-processor-mouse-layer-gate";
    #input-processor-cells = <2>;
    activation-threshold = <30>;   // 累计 |dx|+|dy| 计数
    activation-window-ms = <300>;  // 必须在 300ms 内累计完成
};
```

- 只有累计位移达到 `activation-threshold` 才激活层；达不到就只是正常移动光标，不切层。
- `activation-window-ms` 内没有新移动就清零，避免"慢慢漂移"凑够阈值。
- 当前 300 CPI 下，`30 counts ≈ 2.5mm`。还嫌容易触发就加到 40~50；觉得要"划一下才进"就降到 20。

> 注意：阈值是在 `zip_xy_scaler 1 4` **之后**计算的，也就是 300 CPI 的计数。

---

## 3. 回层时间 0.4s，并在按鼠标层按键时续时

```dts
<&mouse_layer_gate 3 400>;   // param1 = 层号, param2 = 400ms
```

行为：

1. 停止移动 400ms 后自动回到原来的层（不是固定回 layer 0，而是回到进入 mouse 层之前的那层）。
2. 鼠标层上**非 `&trans`** 的按键（`&mkp MB1/MB2/MB3`、`&to 0`、`&lt 1 SPACE` 等）
   按下或松开时会重新计时 400ms。
3. `&trans` 键不会续时。判断方式是运行时查 `zmk_keymap_get_layer_binding_at_idx()`，
   比较 `behavior_dev` 是否为 `"trans"`，所以以后改键位也不用维护位置列表。

> 轨迹球持续移动时也会不断续时；只有"没移动 + 没按鼠标层按键"满 400ms 才回落。

---

## 4. dongle 屏幕显示

屏幕由 `englmaxi/zmk-dongle-display` 的 `dongle_display` shield 绘制（128×64 SH1106）。
当前配置实际会显示：

- 左上：USB / 蓝牙输出状态、HID 是否就绪、蓝牙 profile 编号、连接状态、当前输出选择下划线
- 右上：左右半电池电量（`CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y`）
- 左下：修饰键状态（`CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS=y`，显示 ⌃⌥⌘⇧）
- 右下：Bongo Cat（打字越快动得越快）+ 当前层名（`display-name`：base / func / quick / mouse / slow）
- 未启用：WPM 表（`CONFIG_ZMK_DONGLE_DISPLAY_WPM` 默认 n）

预览图见 `display-preview/`（用固件里的 1-bit 图标和 bongo cat 原始位图渲染的模拟图，
不是实机拍照）。

---

## 5. 最近按键显示（last key overlay）

在 dongle 屏幕中间偏右的空白区加了一个小 overlay：

- 区域：`x = 31..77, y = 19..45`（46×26 px），也就是你画的红色框的下半部分；
  红色框最上面 `y = 0..18` 会压到右上角电池，所以没有用那一段。
- 左边避开 NLCK / HID 指示灯（`x < 31`），右边避开 Bongo Cat（`x >= 78`），
  下方避开修饰键（`y >= 47`）。
- 只显示**最后一次发送给电脑的非修饰键**；纯 Ctrl/Shift/Alt/Gui 不覆盖它，
  因为左下角本来就有修饰键图标。
- 短键（`A`、`Space`、`Enter`）一行居中显示；长键（`PageUp`/`PageDn`）显示两行
  `Page` + `Up` / `Dn`，仍然居中。
- 映射表覆盖字母、数字、F1–F12、方向/编辑键、媒体键（`VOL+`、`MUTE`、`PLAY` 等），
  来自 [`zzuse/zmk-nice-oled-zz`](https://github.com/zzuse/zmk-nice-oled-zz)（MIT），
  已改成适合 46 px 宽度的完整短名。
- Kconfig：`CONFIG_ZMK_KEY_STATUS_OVERLAY`，`default y if ZMK_DISPLAY`，只影响 dongle。
- 源码：`src/key_status_overlay.c`、`src/keycode_name.c` / `.h`。

---

## 编译

GitHub Actions 的 ZMK 构建流程会检测到仓库根目录的 `zephyr/module.yml`，自动把本仓库作为
`ZMK_EXTRA_MODULES` 参与编译，因此 `build.yaml` 不需要改。

本地编译时等价地传：

```bash
west build -s zmk/app -d build/dongle -b nice_nano_v2 \
  -- -DSHIELD="charybdis_central_dongle dongle_display" \
     -DZMK_CONFIG=/path/to/zmk-config-dragonball-NoirGuo/config \
     -DZMK_EXTRA_MODULES=/path/to/zmk-config-dragonball-NoirGuo \
     -DCONFIG_ZMK_STUDIO=y -DCONFIG_ZMK_STUDIO_LOCKING=n
```

启用 Studio 的 snippet 时再加 `-S studio-rpc-usb-uart`。

---

## 改动文件清单

```
CMakeLists.txt                                  新增（模块编译入口）
Kconfig                                         新增（CONFIG_ZMK_INPUT_PROCESSOR_MOUSE_LAYER_GATE）
zephyr/module.yml                               增加 cmake / kconfig / dts_root
src/input_processor_mouse_layer_gate.c          新增（位移门限 + 续时逻辑）
src/key_status_overlay.c                        新增（最近按键 overlay）
src/keycode_name.c / .h                         新增（HID 按键名映射，MIT 改编）
tools/fetch_keycode_name.py                     新增（生成映射表的脚本）
dts/bindings/input_processors/zmk,input-processor-mouse-layer-gate.yaml  新增
config/charybdis.keymap                         新处理器节点、scaler、400ms
boards/shields/charybdis/trackball.dtsi         cpi 400 → 1200
```
