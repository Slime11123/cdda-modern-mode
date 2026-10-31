# UndeadPeople 与中文运行配置

2026-10-26，用户要求先下载 UndeadPeople（不死人）图块包，以此为后续开发主要素材，并将游戏改为中文。

## 来源与安装

下载来源：[eremtsov/UndeadPeopleUnpacked](https://github.com/eremtsov/UndeadPeopleUnpacked)，固定提交 `a821c120cccd898c14484cdbb5c9d52c1231dd40`。原始压缩包存于项目根 `assets/downloads/UndeadPeople-a821c120.zip`，SHA256 为 `21FC4D1D83277EA5B7F6C17EC9FA13572087AE6DB671DC6FF33C0C67F98A3DF3`。

提取基础游戏目录 `!dda` 及构建脚本，使用该包自身的 `compose.py` 生成完整图块；未运行附带的 exe 或批处理。构建依赖仅装在项目 `.tools/tileset-python`，使用 pyvips 3.2.0 / libvips 8.18.7。构建日志 `qa/0i1/undead-compose.log`。

生成目录：项目根 `assets/tilesets/MSX++UnDeadPeopleEdition/`，包含 16 张图块表、21,413 条配置条目、fallback 和 layering。图块 ID 为 `UNDEAD_PEOPLE_BASE`。来源、脚本和输出配置哈希记于 `assets/tilesets/undead-provenance.json`。

默认开发用户目录 `runtime/0i1/user` 的图块选项已设为 `UNDEAD_PEOPLE_BASE`，语言为 `zh_CN`；原配置备份为 `options.json.before-undead-zh`。启动器的新用户默认值同步更新，并会复制项目中生成的素材。原有其他图块仍可用于后续切换回归。

## 实机验证

未修改的 0.I-1 原版程序已实际载入独立世界副本，日志确认 `Loaded tileset: UNDEAD_PEOPLE_BASE` 以及 `Setting language to: 'zh_CN'`。菜单、角色侧栏及保存确认均显示中文；同一避难所地图显示 UndeadPeople 地板、墙、门和人物。截图 `qa/0i1/undead-chinese-world.png`，日志在 `qa/0i1/undead-zh/config/debug.log`。

这是原版 2D 图块加载验证；P3 的第一人称画面目前仍为简化颜色几何，不能把“图块已安装”写成“3D 材质已经映射”。P4 将以这套实际素材为主，处理原版选中图块、缺失素材回退以及切换刷新。
