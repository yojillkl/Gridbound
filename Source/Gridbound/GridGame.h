#pragma once
// Gridbound：第一人称网格战斗原型。AGridPawn 负责玩家、敌人状态机、网格规则与战斗模拟；
// 移动、战斗、关卡、环境与程序化美术拆分到同名的多个 .cpp 文件里。
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GridGame.generated.h"

class UProceduralMeshComponent;

namespace GridRules
{
    constexpr float CellSize = 150.f;          // 单格边长（厘米），所有位置与距离都以它为单位
    constexpr int32 BoardSize = 32;            // 棋盘为 32×32 格
    constexpr int32 MaxHealth = 100;           // 玩家与敌人的生命上限
    constexpr int32 FireballDamage = 50;       // 火球命中角色造成的伤害
    constexpr int32 FireballDemolition = 50;   // 火球命中土柱造成的拆毁值
    constexpr int32 WallDurability = 100;      // 单根土柱的耐久
    constexpr float WallLifetime = 60.f;       // 土柱存在的秒数
    constexpr float WallRiseTime = .35f;       // 土柱从地面升起到顶的时间
    constexpr float BurnLifetime = 10.f;       // 草地燃烧时长
    constexpr float BurnDamagePerSecond = 10.f; // 站在火上的每秒伤害
    constexpr float EyeHeight = 130.f;
    constexpr float ActorOriginHeight = 75.f; // 角色的根节点位于脚底上方 75 厘米
    constexpr float GroundTolerance = 3.f;    // 脚底低于此高度即视为站在地面上
    constexpr float JumpHeight = 320.f; // 跳跃顶点；能越过 300 厘米的遗迹柱顶，从而直接跳上石柱
    constexpr float JumpApexSeconds = .41f;   // 到达顶点的时间，跳跃速度与重力由它推导
    constexpr float JumpSpeed = 2.f * JumpHeight / JumpApexSeconds;
    constexpr float JumpGravity = 2.f * JumpHeight / (JumpApexSeconds * JumpApexSeconds);
    constexpr float Gravity = 980.f;
    constexpr float StepSeconds = .3f;
    constexpr float MaxStepHeight = 20.f;     // 贴地角色可直接跨上的最大高差（竞技场）
    constexpr float LevelStepHeight = 80.f;   // 敌人在关卡中可攀爬比玩家更高的地形
    constexpr float MovementChordWindow = .06f; // 两个方向键被判定为同一次斜向输入的时间窗
    constexpr int32 SlashDamage = 20;         // 一次劈砍对玩家造成的伤害
    constexpr int32 SlashDemolition = 10;     // 一次劈砍对土柱造成的拆毁值
    constexpr float SlashCooldown = 1.f;      // 两次劈砍之间的冷却
    constexpr float MeleeReachExtra = 10.f;   // 劈砍超过一格的水平余量
    constexpr float MeleeReachHeight = 100.f; // 劈砍的垂直容差
    constexpr float MeleeWindup = .45f;       // 劈砍命中前的预警时间
    constexpr float WarriorStepSeconds = .45f; // 敌人走一格的时间
    constexpr float PlayerRespawnDelay = 2.f;  // 玩家死亡后等待重生的秒数
    inline FIntPoint MovementInput(bool Forward, bool Back, bool Left, bool Right)
    { return FIntPoint(int32(Forward)-int32(Back),int32(Right)-int32(Left)); }
    inline FIntPoint MoveDirection(float Yaw, FIntPoint Input)
    {
        // 把「前后左右」输入按镜头朝向旋转，并量化到最近的八方向之一。
        if(Input==FIntPoint::ZeroValue) return FIntPoint::ZeroValue;
        const FVector Desired=FRotator(0,Yaw,0).RotateVector(FVector(Input.X,Input.Y,0));
        const float Angle=FMath::RoundToInt(FMath::Atan2(Desired.Y,Desired.X)/(PI/4.f))*(PI/4.f);
        return FIntPoint(FMath::RoundToInt(FMath::Cos(Angle)),FMath::RoundToInt(FMath::Sin(Angle)));
    }
    constexpr float FireballRange = 9.f * CellSize; // 火球飞行上限；比 10 格的索敌半径少一格
    constexpr float FireballSpeed = 1800.f;         // 火球飞行速度
    constexpr float FireballRadius = 18.f;          // 火球碰撞半径
    constexpr int32 WallRange = 10;   // 土墙中心距离玩家的最大格数
    constexpr int32 RainRadius = 4;   // 降雨覆盖半径（格）
    constexpr int32 RainRange = 10;   // 降雨施法距离（格）
    constexpr float FireballCooldown = 3.f; // 火球是常驻输出，冷却短但也不能乱放
    constexpr float WallCooldown = 10.f;    // 土墙改变地形，放置它需要权衡
    constexpr float RainCooldown = 10.f;    // 降雨灭火并减速一大片，属于一次性决策
    constexpr float RainVisualLifetime = 3.f; // 降雨动画的持续时间
    constexpr float TerrainRecovery = 60.f;   // 焦土/泥地恢复原貌的时间
    inline TArray<FIntPoint> RainCells(FIntPoint CenterCell)
    {
        // 以中心格为圆心的覆盖范围：曼哈顿距离会变成菱形，这里改用欧氏距离。
        TArray<FIntPoint> Cells;
        for(int32 X=-RainRadius;X<=RainRadius;++X) for(int32 Y=-RainRadius;Y<=RainRadius;++Y)
            if(X*X+Y*Y<=RainRadius*RainRadius) Cells.Add(CenterCell+FIntPoint(X,Y));
        return Cells;
    }
    constexpr float WallHeight = 3.f * CellSize; // 土柱总高度
    inline TArray<FIntPoint> WallCells(FIntPoint CenterCell, bool bAlongX)
    {
        // 一面土墙是横跨 5 格的直线，bAlongX 决定沿 X 还是 Y 轴展开。
        TArray<FIntPoint> Cells;
        for(int32 Offset=-2;Offset<=2;++Offset)
            Cells.Add(CenterCell+(bAlongX?FIntPoint(Offset,0):FIntPoint(0,Offset)));
        return Cells;
    }
    inline int32 Distance(FIntPoint A, FIntPoint B) { return FMath::Abs(A.X-B.X)+FMath::Abs(A.Y-B.Y); }
    inline bool Inside(FIntPoint P) { return P.X>=0 && P.Y>=0 && P.X<BoardSize && P.Y<BoardSize; }
    inline FVector Center(FIntPoint P, float Z=0.f) { return FVector(P.X*CellSize,P.Y*CellSize,Z); }
    inline FIntPoint Cell(FVector P) { return FIntPoint(FMath::FloorToInt((P.X+CellSize/2)/CellSize),FMath::FloorToInt((P.Y+CellSize/2)/CellSize)); }
    inline FIntPoint Cardinal(FVector V) { return FMath::Abs(V.X)>=FMath::Abs(V.Y) ? FIntPoint(V.X>=0 ? 1:-1,0) : FIntPoint(0,V.Y>=0 ? 1:-1); }
}

UCLASS()
class AGridPawn : public APawn
{
    GENERATED_BODY()
public:
    AGridPawn();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gridbound|Combat")
    int32 Health = GridRules::MaxHealth;
    FIntPoint CurrentCell = FIntPoint(3,10);
    FIntPoint AimCell = FIntPoint::ZeroValue;
    bool bHasAim = false;
    bool bValidAim = false;
    int32 AimEnemy = INDEX_NONE;
    // 按 Q、E、R 之前没有任何法术被选中。
    int32 SelectedSkill = INDEX_NONE;
    bool bWallAlongX = false;
    float Cooldowns[3] = {0.f,0.f,0.f};
    FString Feedback = TEXT("Q 火球术｜E 土墙术｜R 降雨术｜左键施放");
    // 单个敌人；AI 是在 TickCombatants 里驱动的小型状态机。
    struct FTarget {
        FIntPoint Cell;                              // 当前占据的逻辑格
        int32 Health=100;                            // 自己的生命，与玩家独立
        TWeakObjectPtr<AActor> Actor;                // 视觉代理（冒险者 + 剑）
        float FootHeight=0.f;                        // 脚底相对格面地表的高度
        float FallSpeed=0.f;                         // 悬空时的垂直速度
        float BurnFraction=0.f;                      // 本帧累积的小数火焰伤害
        FIntPoint MoveDestination=FIntPoint::ZeroValue;
        float MoveProgress=0.f;                      // 当前步进 0..1
        bool bWalking=false;                         // 正在从 Cell 走向 MoveDestination
        float AttackCooldown=GridRules::SlashCooldown;
        float SlashRemaining=0.f;                    // 挥剑动画计时
        FIntPoint HomeCell=FIntPoint::ZeroValue;     // 关卡模式下的巡逻锚点
        float AlertTime=0.f;                         // 追击已知玩家位置剩余的秒数
        float ThinkTime=0.f;                         // 让寻路不必每帧都跑一次
        float Windup=0.f;                            // 劈砍命中前的预警计时
        FIntPoint LastKnownPlayer=FIntPoint::ZeroValue;
        FIntPoint StrikeCell=FIntPoint::ZeroValue;   // 本次劈砍将要命中的格
        bool bStrikeWall=false;                      // 为真表示本次劈砍目标是土柱
        TWeakObjectPtr<class UStaticMeshComponent> Sword;
    };
    TArray<FTarget> Targets;
    UPROPERTY(EditAnywhere, Category="Gridbound|Spawning")
    FIntPoint PlayerSpawnCell=FIntPoint(3,10);
    UPROPERTY(EditAnywhere, Category="Gridbound|Spawning")
    FIntPoint EnemySpawnCell=FIntPoint(6,11);
    void ResetArena();
    UPROPERTY(EditAnywhere, Category="Gridbound|Camera", meta=(ClampMin="0.01",ClampMax="2.0"))
    float MouseSensitivity = 0.26f;
    float MovementSpeedMultiplier(FIntPoint Cell, float FeetHeight) const;
private:
    friend class AGridHUD;
    struct FFireball { TWeakObjectPtr<AActor> Actor; FVector Direction; float Remaining=GridRules::FireballRange; }; // Remaining：剩余飞行距离
    struct FWall { FIntPoint Cell; TWeakObjectPtr<AActor> Actor; int32 Durability=GridRules::WallDurability; float Remaining=GridRules::WallLifetime; float Age=0.f; TWeakObjectPtr<UProceduralMeshComponent> Visual; }; // 玩家召唤的土柱
    struct FBurningCell { FIntPoint Cell; float Remaining=GridRules::BurnLifetime; float Age=0.f; TWeakObjectPtr<AActor> Actor; float RecoveryRemaining=GridRules::TerrainRecovery; }; // 燃烧中的草地
    struct FMuddyCell { FIntPoint Cell; float Remaining=GridRules::TerrainRecovery; TWeakObjectPtr<AActor> Actor; }; // 降雨制造的泥地
    struct FRainEffect { TWeakObjectPtr<AActor> Actor; float Age=0.f; }; // 降雨的纯视觉表现
    // Dijkstra 的临时缓冲，跨 EnemyNextStep 调用复用，避免每帧分配内存。
    struct FPathScratch { TArray<FIntPoint> Open; TMap<FIntPoint,FIntPoint> Parents; TMap<FIntPoint,float> Costs; TSet<FIntPoint> Closed; };
    mutable FPathScratch PathScratch;
    TArray<FFireball> Fireballs;
    TArray<FWall> Walls;
    TArray<FBurningCell> BurningCells;
    TArray<FMuddyCell> MuddyCells;
    TArray<FRainEffect> RainEffects;
    TMap<FIntPoint,TWeakObjectPtr<AActor>> TerrainTiles;
    float FootHeight=0.f;       // 玩家脚底相对格面地表的高度
    float FallSpeed=0.f;        // 玩家悬空时的垂直速度
    float BurnFraction=0.f;     // 本帧累积的小数火焰伤害
    float DamageFlash=0.f;      // 0..1，受伤时的红色全屏反馈，在 Tick 里衰减
    float BurnOverlay=0.f;      // 0..1，站在燃烧地面上的橙色全屏反馈
    FVector AimPoint = FVector::ZeroVector; // 屏幕中心射线命中的瞄准点
    TArray<FIntPoint> WallPlan; // 在瞄准校验与每帧土墙预览之间复用的格子列表
    TMap<FIntPoint,int32> OccupiedBy; // 格 -> 敌人索引，每个战斗帧重建，用于 O(1) 的占位查询
    UPROPERTY() TObjectPtr<class UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<class UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInterface> TerrainMaterial;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Adventurer;
    FIntPoint Destination;      // 当前步进的目标格
    float MoveTime = 0.f;       // 当前步进已消耗的时间
    bool bMoving = false;       // 正在从 CurrentCell 走向 Destination
    bool bJumping = false;      // 处于空中；水平移动仍可用
    FIntPoint PendingMoveInput = FIntPoint::ZeroValue; // 缓存等待判定的方向输入
    bool bWaitingForMoveChord = false; // 正在缓冲两个方向键以合并成一次斜向输入
    float MoveChordAge = 0.f;   // 方向键合并缓冲已等待的时间
    float RespawnRemaining = 0.f; // 距离重生的剩余秒数
    bool bLevelMode=false;      // 关卡模式；否则为自由练习的竞技场
    int32 LevelStage=0;         // 关卡阶段：0 拿晶核前，1 拿到后
    bool bLevelComplete=false;  // 晶核已带回营地，通关
    FIntPoint RelicCell=FIntPoint(27,16); // 晶核所在的格（死亡掉落时会更新）
    bool bRelicCarried=false;   // 玩家当前是否携带晶核
    float LevelSeconds=0.f;     // 本关已用时间（用于巡逻相位与结算）
    int32 LevelDeaths=0;        // 本关死亡次数
    int32 LevelCasts[3]={0,0,0}; // 本关三种法术各施放次数
    TArray<TWeakObjectPtr<AActor>> LevelProps;   // 关卡生成的静态地形与装饰
    TArray<TWeakObjectPtr<AActor>> LevelBeacons; // 晶核与营地的光柱标记
    void BuildLevel();
    void ResetLevel();
    void StartEncounter();
    void TickLevel(float DeltaSeconds);
    void TickLevelCombatants(float DeltaSeconds);
    void InteractLevel();
    bool LevelBlocked(FIntPoint Cell) const;
    float LevelHeight(FIntPoint Cell) const;
    bool LevelFireImpact(AActor* Actor);
    bool EnemySeesPlayer(const FTarget& Target) const;
    FString LevelObjective() const;
    bool SpawnWarrior(FIntPoint Cell);
    bool FindSpawnCell(FIntPoint Preferred, bool bForPlayer, FIntPoint& Result) const;
    void TickCombatants(float DeltaSeconds);
    void RespawnPlayer(FIntPoint Cell);
    void RebuildOccupancy();
    bool EnemyCellOccupied(FIntPoint Cell, int32 Self) const;
    bool EnemyNextStep(int32 Index, bool bAllowWalls, FIntPoint& Next, const FIntPoint* GoalOverride=nullptr) const;
    bool TryWarriorSlash(int32 Index, int32 WallIndex=INDEX_NONE);
    void BuildArena();
    AActor* MakeBlock(FVector Location, FVector Scale, FLinearColor Color, bool bCollision=true);
    void UpdateAim();
    void CastSkill(int32 Skill);
    void LaunchFireball(FVector Direction);
    void TickFireballs(float DeltaSeconds);
    void ResolveFireballImpact(AActor* HitActor, FVector ImpactPoint);
    void DamageWall(int32 Index, int32 Demolition);
    void RemoveWall(int32 Index);
    void TickWalls(float DeltaSeconds);
    void TickBurning(float DeltaSeconds);
    bool IgniteCell(FIntPoint Cell);
    float SurfaceHeight(FIntPoint Cell) const;
    void UpdateElevation(float DeltaSeconds);
    FVector SpellOrigin() const;
    void SetupCombatShowcase();
    void SetupRainShowcase();
    bool CanCastRain(FIntPoint CenterCell) const;
    bool CastRain(FIntPoint CenterCell);
    void MakeMud(FIntPoint Cell);
    void TickWetTerrain(float DeltaSeconds);
    void AdvanceMovement(float DeltaSeconds);
    bool TryStartMove(FIntPoint Input, float Yaw);
    void ProcessMovementInput(float DeltaSeconds, FIntPoint Input, bool bHasHeldInput, bool bNewPress, float Yaw);
    bool CanStep(FIntPoint Step) const;
    bool TryJump();
    bool IsGrounded() const;
    void SetTerrainVisible(FIntPoint Cell, bool bVisible);
    TArray<FIntPoint> PlaceableWallCells(FIntPoint CenterCell) const;
    bool CanPlaceWall(FIntPoint CenterCell) const;
    bool PlaceWall(FIntPoint CenterCell);
    bool HasLineOfSight(FIntPoint Cell, const AActor* Target=nullptr) const;
    bool CanEnter(FIntPoint Cell) const;
    void DrawCell(FIntPoint Cell, FColor Color, float Life=0.f) const;
};

UCLASS()
class AGridHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
private:
    UPROPERTY() TObjectPtr<class UFont> ChineseFont;
};

UCLASS()
class AGridGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AGridGameMode();
};
