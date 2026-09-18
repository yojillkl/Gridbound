# v0.3.0 本地构建记录

2026-09-18：恢复并完成此前中断的构建工作。

- GridboundEditor 与 Gridbound Win64 Development 编译成功。
- 8 项 Gridbound 自动化测试通过，报告中 0 失败、0 警告；报告位于 `Validation/UnifiedLevel/index.json`。
- Windows 打包成功，输出 `Releases/v0.3.0/Windows/Gridbound.exe`。
- 独立打包版 DX12 离屏渲染验证中文字体与开局 HUD。检查中发现字体文件构造器不支持 Inline 加载，改为 LazyLoad 后启动成功。
- 修复敌人血条压住玩法文字、营地标记挡住出生镜头的问题。
- 根目录启动脚本指向 v0.3.0；v0.2.0 文件保留。

验证涵盖两处渡口连通性、无需清敌拾取与撤离、局部警戒、晶核死亡掉落与回收、土墙登高、重置、近战预警和闪避。人工键鼠完整通关、平衡性及跨设备性能尚未验收。本次没有上传 GitHub Release。
