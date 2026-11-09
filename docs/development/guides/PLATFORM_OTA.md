# Kimiko 平台 OTA 发布

1. 在 kimiko-platform 后端设置 `KIMIKO_INTEGRATION_FIRMWARE_TOKEN` 为至少 32 字符的随机密钥，并通过 HTTPS 部署；确认机器接口没有其他入口绕过认证。
2. 在本仓库 GitHub Actions 的变量中设置 `KIMIKO_PLATFORM_URL`（HTTPS 网站根地址），在 Secrets 中设置 `KIMIKO_FIRMWARE_SUBMISSION_TOKEN`（与网站相同）。限制有权创建或移动发布标签的人，避免不可信代码借发布工作流读取密钥。
3. 推送 `v1.2.3` 或 `v1.2.3-rc1` 标签。工作流以 ESP-IDF 6.1 的 ESP32-S31 产品配置构建；普通版本进入 stable，rc 版本进入 beta。设备默认读取 stable，beta 不会自动推送给默认设备。
4. 在网站后台检查版本、来源、文件、更新说明和最低基础版本后手动发布。第一次使用联网升级的设备可能需要先通过网页或 TF 卡安装包含升级入口和正确分区布局的引导版本。
5. 在真实设备上验证升级、断网恢复、数据保留后再宣布可用。更换密钥时同步更新网站和 GitHub Secret。不要把密钥提交到仓库。

## 发布附件与 LGPL / Release assets and LGPL

正式标签工作流保留两组同源产物：`firmware-<tag>-<run>` 用于 USB、TF 与 OTA；`lgpl-relink-<tag>-<run>` 保存静态库、应用入口目标文件、链接脚本、原始链接命令、工具链标识与实际使用的托管组件源码。工作流先执行原链接命令并逐字节比较 ELF；不通过就不提交 OTA 草稿。
Official tag runs preserve firmware delivery inputs and a separate LGPL relinking artifact with static libraries, the application entry object, linker scripts, the original link command, toolchain identity and managed-component sources. The original command must reproduce the ELF byte for byte before OTA draft submission.

发布时将该标签的完整源码、两组同次构建产物及许可合成 `kimiko-e4p-<version>-source-relink.zip`，作为同一 GitHub Release 的可下载附件；USB 包和外层说明指向它。不能只保留有期限的 Actions artifact 或仅提供编译后的固件。包内说明应记录原工作目录、IDF 环境与重链接步骤，并包含 LGPL/GPL 许可；修改 LGPL 组件后可重编相应静态库，再使用应用目标代码重新链接。个人学习条款不限制第三方许可保障的修改、调试和重新链接权利。
Publish the complete tagged source, both same-run artifacts and licenses as `kimiko-e4p-<version>-source-relink.zip` beside the firmware Release assets, referenced by the USB and outer instructions. Expiring Actions artifacts or binaries alone are insufficient. Include the original workspace, IDF environment, relinking instructions and LGPL/GPL texts; modified LGPL libraries can be rebuilt and relinked with the application objects. Preserve third-party modification, debugging and relinking rights.
