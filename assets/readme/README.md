# README 界面预览

这些图片用于根 README 的产品展示，不参与固件构建。它们展示当前源码的绘制结果，不是设备实拍、在线天气或已发布版本的验收证据。书名、日期、温度、阅读统计、文件大小与存储容量均为样例数据；不包含用户书籍或个人记录。

## 概览图

`overview.png` 从以下两个现有工具的输出合成，使用源码中的字体、位图与绘制逻辑，不另画一套 UI：

```sh
python3 tools/reading_journal_ui_host_test.py --output build/readme-preview/journal
python3 tools/weather_raster_host_test.py --output-dir build/readme-preview/weather
```

需要 Pillow、fonttools、本机 C 编译器和构建准备的 cJSON 依赖，见[开发上手](../../docs/ONBOARDING.md)。

README 的天气展示城市固定为**上海**。天气工具的默认样例城市不同，重新导出时仅在临时绘图夹具中将 `view.city` 的 `id/name/admin1/admin2/lat/lon` 设为 `101020100 / 上海 / 上海市 / 上海 / 31.2304 / 121.4737`，并将 `view.forecast.location` 同步为该城市；同时检查内置字体覆盖「上海上海市」。不改产品默认设置、线上天气或回归工具的默认夹具。温度等数据仍为工具样例，不代表上海的实时天气。

合成画布为 1464 × 1280，底色 `#f3f4f4`；以下 PNG 按原尺寸粘贴，不拉伸、不重绘文字：

| 图片 | 左上角坐标 |
| :-- | :-- |
| `weather/sun.png` | `(32, 112)` |
| `journal/widget-scene-0.png` | `(32, 672)` |
| `journal/detail-today.png` | `(748, 32)` |

## 传书网页

`transfer.png` 是 [upload.html](../../components/wifi_transfer/upload.html) 在 Chromium 1360 × 900 视口下的截图，使用页面自身的内嵌样式、图标与交互脚本。

复现时在浏览器拦截 `http://kimiko-preview.test/` 并返回该 HTML，对以下只读请求返回样例 JSON，再点击「书籍」并截图；不要连接真实设备，也不发起上传、删除或保存：

| 请求 | 样例响应 |
| :-- | :-- |
| `/info` | `{"mode":"ap","is_flash":false,"free_bytes":12884901888,"file_limit":52428800}` |
| `/signature` | `{"signature":"把时间留给阅读"}` |
| `/files`（包括分页及路径参数） | `pages: 1`、`total: 4`，`items` 为下表 |

| `name` | `directory` | `size` |
| :-- | :-- | --: |
| 散文 | `true` | 0 |
| 朝花夕拾.epub | `false` | 1572864 |
| 诗经.txt | `false` | 245760 |
| 阅读示例.epub | `false` | 3145728 |

两张图片的源码基线为 `c5e9a1e`；界面发生变化时应重新生成，不在截图上修补控件或文字。README 中的 logo 直接引用 [默认品牌图片](../image/README.md)，不复制另一份。各项资源继续遵循[根许可](../../LICENSE)及[第三方声明](../../THIRD_PARTY_NOTICES.md)，不额外授予图片或上游资源的许可。
