# 精细风格化奇幻美术

本版用程序化模型与材质实现统一的奇幻风格。手绘质感来自连续的顶点配色和细微笔触纹理，并非外部手绘贴图。

- 人物：收腰分层胸甲、较薄的护肩、护胫、面甲孔槽、剑柄缠线、暗红披风和纹章战袍。金属采用哑光旧钢配色。
- 第一人称：连续弯曲的指节、扁平掌形、皮手套和低饱和袖口；左右手姿态错开，去掉夸张金边和发光宝石。
- 环境：草与碎石按连续噪声形成疏密不同的斑块，路面留出空地；石墙错缝砌筑；减少成排树干和规则水纹。
- 光照：暖色主光、冷色补光、天空环境光、远景薄雾与克制的泛光。
- 技能：火焰和闪电使用独立发光材质；降雨云团改为圆润体积；范围边界增加向内刻度和中心符号。
- 界面：深青灰面板、低对比细边、统一的技能选中与冷却配色。

美术组件不参与碰撞。战场布局、角色碰撞半径、技能伤害、潮湿/燃烧/导电持续时间和战士技能逻辑均保持原有规则。

材质生成源：`Build/CreateFantasyMaterials.py`。编辑器中运行该脚本可重建五种 `/Game/Art/Materials/M_Fantasy*` 材质；生成的资产随项目提供，无需玩家运行脚本。

启动：`Play-Latest.cmd` 或 `Play-FantasyArt.cmd`。

2026-10-03 修正：针对“拼装感”和重复装饰做了一轮减法，当前启动入口指向 `Releases/NaturalArt`。上一版 `Releases/FantasyArt` 保留。该修正仍基于程序化模型，不等同于完成了专业角色建模与手绘贴图制作。

验证：Windows Development 打包成功；原有自动化用例 `Gridbound.Combat.LightningAndAim`、`Gridbound.Combat.WarriorDuel`、`Gridbound.Movement.FreeHeading` 全部通过。打包程序的决斗与导电场景启动检查正常，截图保存在 `Validation/FantasyArt/`。

修正版验证：`NaturalArt` 打包成功；决斗及闪电展示程序均以退出码 0 结束，未发现材质编译失败或致命运行错误。最新实机截图为 `Validation/NaturalArt/Duel.png` 与 `Validation/NaturalArt/Lightning.png`。本轮只调整表现层，没有重跑先前已通过的玩法自动化用例。
