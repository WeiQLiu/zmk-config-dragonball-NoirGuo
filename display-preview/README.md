# dongle 屏幕模拟图

这不是实机拍照，而是用固件里的原始资源渲染出来的 1:1 模拟图：

- 128×64 画布
- Bongo Cat、USB/蓝牙/修饰键图标：直接解析 `zmk-dongle-display` 里的
  `LV_IMG_CF_INDEXED_1BIT` 位图
- 文字：直接解析 LVGL 的 `lv_font_unscii_8` 字模（固件 `custom_status_screen.c`
  里 `lv_style_set_text_font(&global_style, &lv_font_unscii_8)` 指定的字体）
- 布局：按 `custom_status_screen.c` / 各 widget 的 `lv_obj_align*` 计算

所以能比较准确地反映"固件会在屏幕上画什么"，但 LVGL 的 padding、滚动标签、
图标透明区域等细节无法完全复刻。

## 文件

| 文件 | 内容 |
| --- | --- |
| `dongle-display-preview.png` | 两张状态并排的总览 |
| `dongle-display-base.png` | 默认层 `base`，USB 输出，左右半 87% / 92% |
| `dongle-display-mouse.png` | 轨迹球移动后自动进入 `mouse` 层，Cmd 按下 |
| `*-1x.png` | 原始 128×64 像素图 |

## 屏幕分区

```
+--------------------------------------------------------------+
| USB/BT 输出状态 + profile + 连接状态        右半/左半电池 %    |
|                                                              |
|                                                              |
|                                                              |
| 修饰键图标 (⌃ ⌥ ⌘ ⇧)                    Bongo Cat            |
|                                         当前层名 (base/mouse) |
+--------------------------------------------------------------+
```

| 区域 | 来源 widget | 说明 |
| --- | --- | --- |
| 左上 | `output_status` | USB / BLE 图标、HID ready 对勾/叉、profile 数字、配对/连接状态、当前输出下划线 |
| 右上 | `battery_status` | 两个半边的电量；`CONFIG_ZMK_DONGLE_DISPLAY_DONGLE_BATTERY` 未开，所以不显示 dongle 自己 |
| 左下 | `modifiers` | `CONFIG_ZMK_DONGLE_DISPLAY_MAC_MODIFIERS=y`，所以是 ⌃⌥⌘⇧ 顺序 |
| 右下 | `bongo_cat` + `layer_status` | 打字越快猫动得越快；层名来自 keymap 的 `display-name` |

## 重新生成

```powershell
D:\python\python.exe render_display.py
```

脚本只读固件源码，不会修改仓库里的任何配置。
