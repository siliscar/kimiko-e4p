# 默认品牌图片 / Default Brand Images

`kimiko-logo.png` 来自用户提供的同名 logo，压缩为 600×600、16级灰阶 PNG；保留原图内容、比例和白底，原始上传文件不修改。图片权利归原权利人，不额外授予第三方许可。
`kimiko-logo.png` derives from the user-supplied logo, reduced to a 600x600, 16-level grayscale PNG while preserving its content, aspect ratio and white background. The uploaded original is unchanged; image rights remain with their owner, with no additional third-party grant.

`lock-screen.png` 保持原锁屏的 684×1216 尺寸，logo 为居中的 588×588 图层；默认锁屏和开机准备画面共用其唯一压缩资源。首页60px及设置82px默认头像从同一 logo 生成，留白避免主体被圆形头像边界裁切。自定义头像和壁纸的选择、解码、缓存保持不变。
`lock-screen.png` retains the original 684x1216 dimensions with a centered 588x588 logo. Fallback lock and startup screens share one compressed resource. The 60px Home and 82px Settings avatars derive from the same logo, inset to keep the subject inside circular avatar bounds. Custom avatar/wallpaper selection, decoding and caching are unchanged.

使用已安装 Pillow 的 Python：/ Use Python with Pillow installed:

```sh
python3 tools/generate_brand_images.py --logo /path/to/kimiko-logo.png
python3 tools/generate_brand_images.py --check
python3 tools/builtin_image_host_test.py
```

不传 `--logo` 时从已压缩的源图重新生成。生成的 `main/assets/lock_4bpp.bin` 与 `avatar_4bpp.bin` 都是左像素在低半字节的4bpp数据；构建时仅将全屏图无损压缩为 KBI1/zlib，两个小头像合计5162字节、无需运行时解码。没有独立 `init-loading.png` 或 `loading_4bpp.bin`。
Without `--logo`, regenerate from the checked-in compressed source. The generated lock and avatar binaries use low-nibble-first 4bpp pixels. Builds losslessly compress only the full-screen image as KBI1/zlib; the two small avatars total 5162 bytes and need no runtime decoding. There is no separate `init-loading.png` or `loading_4bpp.bin`.
