# 2D / 3D 渲染互斥

2026-10-31。

2D 经典和 2D 现代模式均不采集 3D 场景，不准备 3D 材质，也不执行 3D 绘制。原版菜单接管屏幕时，同样跳过场景采集与绘制。

3D 场景覆盖原版地图窗口时，跳过原版地图底色、图块绘制缓存生成、文字/色块叠加，以及脚步、命中、光标等 2D 动画绘制。瞬时动画按原版生命周期清理，持续动画仍由调用方管理。3D 快照在原版地图绘制之前发布，切入 3D 的首帧也可跳过隐藏的地图绘制。

保留游戏视线、光照、探索记忆更新，以及侧边栏和菜单。2D 绘制缓存未更新时保留脏标志，切回 2D 后重新生成。

验证：

- `qa/0i1/render-exclusive-cpu.log`：68 用例、2031 断言通过。
- `qa/0i1/render-exclusive-gl.log`：真实 OpenGL 隐藏窗口测试，1240 断言通过。两种图块分别验证覆盖状态下整帧像素不变、2D 缓存延后、探索记忆保留，以及切回 2D 后正常绘制。
- `qa/0i1/render-exclusive-build.log`、`render-exclusive-tests-build.log`、`render-exclusive-off-build.log`：游戏 ON、测试 ON 和游戏 OFF 增量构建通过。

默认“启动3D技术验证.cmd”使用 `runtime/render-exclusive`，存档和配置沿用 `runtime/0i1/user`。重启游戏生效。未量化实际游玩帧率提升。
