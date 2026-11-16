# UI 素材来源与许可

本项目的 AI 磁贴（控制中心）使用 **Microsoft Fluent Emoji（3D 风格）**，中文界面使用 **Noto Sans SC** 字体子集。V0.4 起表盘不再展示电子宠物，表情只在控制中心 AI 磁贴中轮播。

## 1. Microsoft Fluent Emoji

- 仓库：<https://github.com/microsoft/fluentui-emoji>
- 许可：MIT License（Copyright (c) Microsoft Corporation）
- 使用方式：取 3D 版本 PNG，缩放到 160×160，转为 LVGL `RGB565A8` C 数组
- 本项目使用的 6 个表情：

| 文件名 | 用途 | 上游路径 |
|---|---|---|
| `pip_face_happy` | AI 磁贴默认（开心） | `assets/Smiling face with smiling eyes/3D/smiling_face_with_smiling_eyes_3d.png` |
| `pip_face_thinking` | AI 磁贴·思考 | `assets/Thinking face/3D/thinking_face_3d.png` |
| `pip_face_speaking` | AI 磁贴·说话 | `assets/Face with open mouth/3D/face_with_open_mouth_3d.png` |
| `pip_face_excited` | AI 磁贴·兴奋 | `assets/Star-struck/3D/star-struck_3d.png` |
| `pip_face_sleepy` | AI 磁贴·困了 | `assets/Sleeping face/3D/sleeping_face_3d.png` |
| `pip_face_sad` | AI 磁贴·难过 | `assets/Pleading face/3D/pleading_face_3d.png` |

生成文件：`firmware/ui_demo/main/assets/pip_faces.c` / `pip_faces.h`
生成脚本：`firmware/ui_demo/tools/png_to_lvgl.py`

## 2. Noto Sans SC

- 仓库：<https://github.com/notofonts/noto-cjk>
- 许可：SIL Open Font License 1.1
- 使用方式：下载 `Sans/SubsetOTF/SC/NotoSansSC-Regular.otf`，用 `lv_font_conv` 只打包界面用到的汉字 + ASCII，生成 `epw_cjk_16.c` / `epw_cjk_20.c`
- 生成工具：<https://github.com/lvgl/lv_font_conv>（MIT）

## 3. 重新生成

```bash
cd projects/english-pet-watch/firmware/ui_demo
./tools/gen_ui_assets.sh
```

脚本会下载上游素材到临时目录，重新生成字体与表情 C 资源；不把 8MB 源字体提交进仓库。
