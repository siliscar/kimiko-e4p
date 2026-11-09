<!--
SPDX-FileCopyrightText: 2026 chopper1026
SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
-->

# 阅读手账像素图 / Reading Journal Scenes

六幅105×81灰阶PNG为设备原图，以2倍整数绘制到210×162区域，不插值。空记录与非空记录使用相同场景；六图日更、手动选择及保存格式不变。旧六幅图及阅读角空景已移除。

The six 105x81 grayscale PNGs are the device sources, drawn without interpolation at integer 2x scale into the existing 210x162 area. Empty and populated journals share the selected scene. Daily/manual rotation and persistence remain unchanged; the old six scenes and empty corner are removed.

| 文件 / File | 主题 / Theme | PixelFX部分 / PixelFX Portion |
| --- | --- | --- |
| `slow-train.png` | 沿途有书 / Books Along The Way | 窗外雨滴 / Window rain |
| `corner-bookshop.png` | 街角书店 / Corner Bookshop | 门外落叶 / Falling leaves |
| `paper-garden.png` | 纸上花园 / Paper Garden | 书页瀑布 / Waterfall |
| `moon-observatory.png` | 月面观测站 / Moon Observatory | 无，原创构图 / None, original composition |
| `vinyl-room.png` | 唱片聆听角 / Vinyl Listening Corner | 无，原创构图 / None, original composition |
| `lighthouse-library.png` | 灯塔书屋 / Lighthouse Library | 海面涟漪亮纹 / Ripple crests |

## 生成 / Packing

`python3 tools/generate_reading_scenes.py`校验PNG清单、顺序、SHA-256、尺寸、灰阶及不透明度，生成`main/assets/reading_scenes.h`；`--check`验证可复现性。六级源灰度为17、68、136、187、221、255，均可由4bpp无损表示。每行53字节，左像素在低半字节，奇数宽度的最后高半字节补白；每幅4293字节，总计25758字节。不需要在设备或构建机安装PixelFX、Node或联网。

The packer validates the PNG set, order, SHA-256, dimensions, grayscale and opacity before generating the header; `--check` verifies reproducibility. All six source grays map losslessly to 4bpp. Each 53-byte row stores its left pixel in the low nibble and pads the final unused high nibble with white: 4293 bytes per scene, 25758 total. Firmware and builds require no PixelFX, Node or network access.

## 来源与许可 / Sources And Licensing

雨、落叶、瀑布与涟漪使用[PixelFX](https://github.com/patch-gg/pixelfx)的静态帧，固定提交为`a84159688af3ca37a4be05a0d422b94451b2e24e`；按原点绘制、关闭发光，裁切后以最近邻缩放并适配六级灰度。`scenes.json`记录每幅图的效果文件、时间、种子、调色板、裁切、蒙版/补绘说明及PNG校验值。PixelFX不负责建筑、物件或完整构图，不宣称六幅均由它生成；不包含其`gallery/fonts`字体，也不将动态效果或JavaScript带入固件。

Rain, leaves, waterfall and ripple details use static PixelFX frames at the pinned revision, drawn at their declared origins without glow, cropped, nearest-neighbor resized and adapted to six grays. The manifest records effect files, time, seeds, palettes, crops, masking/adaptation and PNG hashes. PixelFX does not supply the buildings, objects or full compositions. Its gallery font is excluded, and no animation or JavaScript is embedded in firmware.

PixelFX部分保留Patch与Hugo Duprez的MIT授权，原文见[LICENSE](PixelFX-LICENSE.txt)及[NOTICE](PixelFX-NOTICE.md)。新增主体、物件及完整构图版权为2026 chopper1026，适用`LicenseRef-Kiikoread-Personal-Study-1.0`；生成表的组合许可按各自部分分别适用，不限制第三方独立授予的权利。历史Kenney插画不再内置，`Kenney-License.txt`仅保留旧版本的原始许可，不表示新图仍使用其图块。

PixelFX portions retain Patch's and Hugo Duprez's MIT grants with the original license and notice. New subjects, props and compositions are copyright 2026 chopper1026 under the personal-study license. The generated header's combined license applies to the respective portions without restricting independently granted third-party rights. Historical Kenney art is no longer bundled; its original license remains solely for prior versions.
