#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"

// 本文件承载 AGridPawn 的主干：出生/初始化、伤害、移动合法性、法术施放、火球、
// 土墙、每帧 Tick 以及 HUD。移动、敌人、关卡、环境与美术逻辑分别拆到其他 .cpp。
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Gridbound, "Gridbound");

AGridGameMode::AGridGameMode()
{
    DefaultPawnClass=AGridPawn::StaticClass();
    HUDClass=AGridHUD::StaticClass();
}

AGridPawn::AGridPawn()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(RootComponent);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetRelativeScale3D(FVector(.55f,.55f,1.4f));
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(FVector(0,0,GridRules::EyeHeight-GridRules::ActorOriginHeight));
    Camera->bUsePawnControlRotation=true;
    Camera->FieldOfView=90.f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh=Cube.Object; SphereMesh=Sphere.Object; BaseMaterial=Mat.Object;
    Body->SetStaticMesh(SphereMesh);
}

void AGridPawn::BeginPlay()
{
    Super::BeginPlay();
    // 通过命令行参数切换模式：默认关卡，Sandbox/展示参数则进入对应的自由场景。
    auto* Mat=UMaterialInstanceDynamic::Create(BaseMaterial,this);
    Mat->SetVectorParameterValue(TEXT("Color"),FLinearColor(0.08f,0.65f,0.95f));
    Body->SetMaterial(0,Mat);
    TerrainMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_FacetedTile.M_FacetedTile"));
    if(!TerrainMaterial) TerrainMaterial=BaseMaterial;
    Body->SetVisibility(false);
    Adventurer=GridArt::CreateAdventurer(this,RootComponent,TerrainMaterial,false);
    Adventurer->SetOwnerNoSee(true);
    bLevelMode=!FParse::Param(FCommandLine::Get(),TEXT("GridboundSandbox"))
        && !FParse::Param(FCommandLine::Get(),TEXT("GridboundCombatShowcase"))
        && !FParse::Param(FCommandLine::Get(),TEXT("GridboundRainShowcase"));
    if(bLevelMode) PlayerSpawnCell=FIntPoint(2,10);
    BuildArena(); ResetArena();
    if(auto* PC=Cast<APlayerController>(GetController()))
    {
        PC->SetControlRotation(FRotator(-12,0,0));
        PC->bShowMouseCursor=false;
        PC->SetInputMode(FInputModeGameOnly());
    }
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundCombatShowcase"))) SetupCombatShowcase();
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundRainShowcase"))) SetupRainShowcase();
#endif
}

AActor* AGridPawn::MakeBlock(FVector Location,FVector Scale,FLinearColor Color,bool bCollision)
{
    // 生成一个纯色方块 Actor，作为敌人、土柱、地形等实体的通用视觉/碰撞载体。
    AActor* A=GetWorld()->SpawnActor<AActor>(Location,FRotator::ZeroRotator);
    auto* Mesh=NewObject<UStaticMeshComponent>(A);
    A->SetRootComponent(Mesh); A->AddInstanceComponent(Mesh);
    Mesh->SetStaticMesh(CubeMesh);
    Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    auto* Mat=UMaterialInstanceDynamic::Create(BaseMaterial,A);
    Mat->SetVectorParameterValue(TEXT("Color"),Color);
    Mesh->SetMaterial(0,Mat); Mesh->RegisterComponent();
    A->SetActorLocation(Location); A->SetActorScale3D(Scale);
    return A;
}

void AGridPawn::BuildArena()
{
    for(int32 X=0;X<GridRules::BoardSize;++X)
    for(int32 Y=0;Y<GridRules::BoardSize;++Y)
    {
        TerrainTiles.Add(FIntPoint(X,Y),GridArt::CreateTile(GetWorld(),FIntPoint(X,Y),TerrainMaterial));
    }
    auto* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,800),FRotator(-60,-30,0));
    Light->GetLightComponent()->SetIntensity(4.f);
    Cast<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(10);
    Cast<UDirectionalLightComponent>(Light->GetLightComponent())->SetAtmosphereSunLight(true);
    auto* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,800),FRotator(-35,150,0));
    Fill->GetLightComponent()->SetIntensity(1.2f);
    Cast<UDirectionalLightComponent>(Fill->GetLightComponent())->SetForwardShadingPriority(0);
    Cast<UDirectionalLightComponent>(Fill->GetLightComponent())->SetAtmosphereSunLight(false);
    Fill->GetLightComponent()->SetCastShadows(false);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    // 无碰撞的分队出生点标记（玩家蓝、敌人红）。
    for(bool bEnemy:{false,true})
    {
        if(bLevelMode && bEnemy) continue;
        const FIntPoint Cell=bEnemy?EnemySpawnCell:PlayerSpawnCell;
        AActor* Pad=MakeBlock(GridRules::Center(Cell,3),FVector(1.1f,1.1f,.04f),
            bEnemy?FLinearColor(.8f,.12f,.035f):FLinearColor(.025f,.65f,1.f),false);
        Pad->Tags.Add(TEXT("GridSpawnPad"));
    }
    // 用一块不发光的球体背景盖住棋盘外缘的下方地平线。
    if(auto* SkyMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_SkyBackdrop.M_SkyBackdrop")))
    {
        AActor* Sky=MakeBlock(FVector(1400,1400,0),FVector(1000),FLinearColor::White,false);
        auto* SkyMesh=Cast<UStaticMeshComponent>(Sky->GetRootComponent());
        SkyMesh->SetStaticMesh(SphereMesh);
        SkyMesh->SetMaterial(0,SkyMaterial);
        SkyMesh->SetCastShadow(false);
    }
    UE_LOG(LogTemp,Display,TEXT("GRIDBOUND_ART_READY tiles=400 obstacles=0 primaryLightPriority=10 fillLightPriority=0 sensitivity=%.2f"),MouseSensitivity);
}

void AGridPawn::ResetArena()
{
    for(auto& Mud:MuddyCells) if(Mud.Actor.IsValid()) Mud.Actor->Destroy();
    MuddyCells.Reset();
    for(auto& Rain:RainEffects) if(Rain.Actor.IsValid()) Rain.Actor->Destroy();
    RainEffects.Reset();
    for(auto& Burning:BurningCells) if(Burning.Actor.IsValid()) Burning.Actor->Destroy();
    BurningCells.Reset();
    for(auto& Tile:TerrainTiles) if(Tile.Value.IsValid())
        if(auto* Mesh=Cast<UPrimitiveComponent>(Tile.Value->GetRootComponent())) Mesh->SetVisibility(true);
    for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("GridSpellDebris"))) It->Destroy();
    for(auto& F:Fireballs) if(F.Actor.IsValid()) F.Actor->Destroy();
    Fireballs.Reset();
    for(auto& Wall:Walls) if(Wall.Actor.IsValid()) Wall.Actor->Destroy();
    Walls.Reset();
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset();
    CurrentCell=PlayerSpawnCell; Destination=CurrentCell;
    SetActorLocation(GridRules::Center(CurrentCell,GridRules::ActorOriginHeight));
    bMoving=false; MoveTime=0; Cooldowns[0]=Cooldowns[1]=Cooldowns[2]=0;
    Health=GridRules::MaxHealth; SelectedSkill=INDEX_NONE; bWallAlongX=false;
    FootHeight=0.f; FallSpeed=0.f; BurnFraction=0.f; bJumping=false;
    PendingMoveInput=FIntPoint::ZeroValue;
    bWaitingForMoveChord=false; MoveChordAge=0.f;
    RespawnRemaining=0.f;
    for(auto Cell:{EnemySpawnCell,FIntPoint(9,7),FIntPoint(10,7)})
    {
        FIntPoint SafeCell;
        if(FindSpawnCell(Cell,false,SafeCell)) SpawnWarrior(SafeCell);
    }
    Feedback=TEXT("Q 火球术｜E 土墙术｜R 降雨术｜左键施放｜F5 重来");
    if(bLevelMode) ResetLevel();
}

float AGridPawn::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    // 统一的生命扣除入口：非法/非正伤害忽略，扣到 0 触发死亡重生流程。
    if(!FMath::IsFinite(DamageAmount) || DamageAmount<=0.f || Health<=0 || (bLevelMode && bLevelComplete)) return 0.f;
    const int32 Applied=FMath::Min(Health,FMath::CeilToInt(FMath::Min(DamageAmount,float(GridRules::MaxHealth))));
    Health-=Applied;
    DamageFlash=1.f;
    if(Health==0)
    {
        if(bLevelMode) ++LevelDeaths;
        RespawnRemaining=GridRules::PlayerRespawnDelay;
        bMoving=false; bWaitingForMoveChord=false; PendingMoveInput=FIntPoint::ZeroValue;
        Feedback=TEXT("你已倒下，2 秒后重生……");
    }
    return float(Applied);
}

bool AGridPawn::CanEnter(FIntPoint Cell) const
{
    // 玩家能否踏入某格：棋盘内、非阻挡、高差不超步高、且没有被存活敌人占据。
    if(!GridRules::Inside(Cell)) return false;
    if(bLevelMode)
    {
        // 河面阻挡贴地步行，但跳跃时可以从上面越过；悬崖和晶核门禁始终是实心。
        const bool bHoppingRiver=GridArt::TerrainAt(Cell)==GridArt::ETerrain::River && bJumping;
        if(LevelBlocked(Cell) && !bHoppingRiver) return false;
    }
    if(SurfaceHeight(Cell)>FootHeight+GridRules::MaxStepHeight) return false;
    for(const auto& T:Targets) if(T.Health>0 && (T.Cell==Cell || (T.bWalking && T.MoveDestination==Cell))) return false;
    return true;
}

bool AGridPawn::HasLineOfSight(FIntPoint Cell,const AActor* Target) const
{
    // 从镜头中心射线判断目标格/目标角色是否被地形或土柱遮挡。
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    if(Target) Params.AddIgnoredActor(Target);
    FHitResult Hit;
    const FVector End=Target?Target->GetActorLocation():GridRules::Center(Cell,SurfaceHeight(Cell)+5.f);
    return !GetWorld()->LineTraceSingleByChannel(Hit,SpellOrigin(),End,ECC_Visibility,Params);
}

void AGridPawn::DrawCell(FIntPoint Cell,FColor Color,float Life) const
{
    const FVector C=GridRules::Center(Cell,SurfaceHeight(Cell)+4);
    const float H=GridRules::CellSize*.48f;
    const FVector Corners[]={C+FVector(-H,-H,0),C+FVector(H,-H,0),C+FVector(H,H,0),C+FVector(-H,H,0)};
    for(int I=0;I<4;++I) DrawDebugLine(GetWorld(),Corners[I],Corners[(I+1)%4],Color,false,Life,0,3.f);
}

void AGridPawn::UpdateAim()
{
    bHasAim=false; bValidAim=false; AimEnemy=INDEX_NONE;
    if(SelectedSkill==INDEX_NONE || Health<=0) return;
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    int32 W,H; PC->GetViewportSize(W,H);
    FVector Origin,Direction;
    if(!PC->DeprojectScreenPositionToWorld(W*.5f,H*.5f,Origin,Direction)) return;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    FHitResult Hit;
    const bool bHit=GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*15000.f,ECC_Visibility,Params);
    AimPoint=bHit?Hit.ImpactPoint:Origin+Direction*15000.f;
    if(SelectedSkill==0)
    {
        // 火球只需一个非零方向即可施放，射程由火球自身的飞行距离决定。
        bHasAim=true;
        bValidAim=!(AimPoint-SpellOrigin()).IsNearlyZero();
        return;
    }
    if(!bHit) return;
    for(int I=0;I<Targets.Num();++I) if(Targets[I].Health>0 && Targets[I].Actor.Get()==Hit.GetActor()) AimEnemy=I;
    if(AimEnemy!=INDEX_NONE) AimCell=Targets[AimEnemy].Cell;
    else
    {
        // 已有土柱也可作为瞄准目标：被占用的格会在土墙摆放时被剔除。
        bool bWallHit=false;
        for(const auto& Wall:Walls) if(Wall.Actor.Get()==Hit.GetActor()) { AimCell=Wall.Cell; bWallHit=true; break; }
        if(!bWallHit)
        {
            if(Hit.ImpactNormal.Z<.9f || Hit.ImpactPoint.Z>10.f) return;
            AimCell=GridRules::Cell(Hit.ImpactPoint);
        }
    }
    bHasAim=GridRules::Inside(AimCell);
    if(!bHasAim) return;
    if(SelectedSkill==1)
    {
        WallPlan=PlaceableWallCells(AimCell);
        bValidAim=!WallPlan.IsEmpty();
    }
    else bValidAim=CanCastRain(AimCell);
}

void AGridPawn::CastSkill(int32 Skill)
{
    // 施放选中技能：0 火球、1 土墙、2 降雨。施放成功后各自进入冷却。
    if(Skill<0 || Skill>2 || Health<=0) return;
    SelectedSkill=Skill; UpdateAim();
    if(Cooldowns[Skill]>0) { Feedback=TEXT("技能正在冷却"); return; }
    if(!bValidAim) { Feedback=TEXT("无法施放：请检查距离、地面和障碍物"); return; }
    if(Skill==0)
    {
        LaunchFireball((AimPoint-SpellOrigin()).GetSafeNormal());
        Feedback=FString::Printf(TEXT("火球：造成 %d 伤害或 %d 拆毁值"),GridRules::FireballDamage,GridRules::FireballDemolition);
    }
    else if(Skill==1)
    {
        const int32 Before=Walls.Num();
        if(!PlaceWall(AimCell)) { Feedback=TEXT("此处无法升起土墙"); return; }
        Feedback=FString::Printf(TEXT("土墙：已升起 %d / 5 根土柱"),Walls.Num()-Before);
    }
    else
    {
        if(!CastRain(AimCell)) { Feedback=TEXT("降雨目标超出范围或被遮挡"); return; }
        Feedback=TEXT("降雨可灭火；泥地使移动减速一半，持续 60 秒");
    }
    Cooldowns[Skill]=Skill==0?GridRules::FireballCooldown:Skill==1?GridRules::WallCooldown:GridRules::RainCooldown;
    if(bLevelMode) ++LevelCasts[Skill];
}

void AGridPawn::LaunchFireball(FVector Direction)
{
    // 生成一个沿指定方向飞行的火球实体，命中与射程结算交给 TickFireballs。
    if(Direction.IsNearlyZero()) return;
    AActor* Actor=MakeBlock(SpellOrigin(),FVector::OneVector,FLinearColor(1.f,.18f,.015f),false);
    Cast<UStaticMeshComponent>(Actor->GetRootComponent())->SetVisibility(false);
    GridArt::CreateFireball(Actor,Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    Actor->SetActorRotation(Direction.Rotation());
    FFireball Fireball; Fireball.Actor=Actor; Fireball.Direction=Direction.GetSafeNormal();
    Fireballs.Add(Fireball);
}

void AGridPawn::TickFireballs(float DeltaSeconds)
{
    // 火球沿直线飞行，用球形扫掠检测命中；耗尽射程或命中即销毁。
    for(int32 I=Fireballs.Num()-1;I>=0;--I)
    {
        auto& Fireball=Fireballs[I];
        if(!Fireball.Actor.IsValid()) { Fireballs.RemoveAtSwap(I); continue; }
        const FVector Start=Fireball.Actor->GetActorLocation();
        const float Travel=FMath::Min(GridRules::FireballSpeed*FMath::Max(0.f,DeltaSeconds),Fireball.Remaining);
        const FVector End=Start+Fireball.Direction*Travel;
        FCollisionQueryParams Params; Params.AddIgnoredActor(this); Params.AddIgnoredActor(Fireball.Actor.Get());
        FHitResult Hit;
        const bool bHit=GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(GridRules::FireballRadius),Params);
        Fireball.Remaining-=Travel;
        if(bHit)
        {
            ResolveFireballImpact(Hit.GetActor(),Hit.ImpactPoint);
        }
        if(bHit || Fireball.Remaining<=0.f)
        {
            Fireball.Actor->Destroy(); Fireballs.RemoveAtSwap(I);
        }
        else
        {
            Fireball.Actor->SetActorLocation(End);
            Fireball.Actor->SetActorScale3D(FVector(1.f+.06f*FMath::Sin(Fireball.Remaining*.1f)));
        }
    }
}

bool AGridPawn::CanPlaceWall(FIntPoint CenterCell) const
{
    return !PlaceableWallCells(CenterCell).IsEmpty();
}

bool AGridPawn::PlaceWall(FIntPoint CenterCell)
{
    // 先在生成前算完整份土墙方案：新土柱不能挡住同批的其他土柱。
    const auto Cells=PlaceableWallCells(CenterCell);
    if(Cells.IsEmpty()) return false;
    if(bMoving && (Cells.Contains(CurrentCell) || Cells.Contains(Destination)))
    {
        CurrentCell=GridRules::Cell(GetActorLocation()); Destination=CurrentCell;
        bMoving=false; MoveTime=0; PendingMoveInput=FIntPoint::ZeroValue;
        bWaitingForMoveChord=false; MoveChordAge=0.f;
        SetActorLocation(GridRules::Center(CurrentCell,FootHeight+GridRules::ActorOriginHeight));
    }
    for(auto& T:Targets) if(T.Health>0 && T.bWalking && T.Actor.IsValid()
        && (Cells.Contains(T.Cell) || Cells.Contains(T.MoveDestination)))
    {
        T.Cell=GridRules::Cell(T.Actor->GetActorLocation()); T.MoveDestination=T.Cell;
        T.bWalking=false; T.MoveProgress=0;
        T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight));
    }
    for(const FIntPoint Cell:Cells)
    {
        FWall Wall; Wall.Cell=Cell;
        Wall.Actor=MakeBlock(GridRules::Center(Cell,GridRules::WallHeight*.5f),
            FVector(GridRules::CellSize/100.f,GridRules::CellSize/100.f,GridRules::WallHeight/100.f),FLinearColor(.34f,.19f,.075f));
        Cast<UStaticMeshComponent>(Wall.Actor->GetRootComponent())->SetVisibility(false);
        Wall.Visual=GridArt::CreateEarthPillar(Wall.Actor.Get(),Wall.Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),Cell,Wall.Durability);
        Wall.Visual->SetAbsolute(false,false,true);
        Walls.Add(Wall);
    }
    TickWalls(0.f); UpdateElevation(0.f);
    return true;
}

void AGridPawn::Tick(float DT)
{
    Super::Tick(DT);
    DamageFlash=FMath::Max(0.f,DamageFlash-DT*2.5f);
    TickWalls(DT);
    UpdateElevation(DT);
    TickBurning(DT);
    TickWetTerrain(DT);
    TickFireballs(DT);
    TickCombatants(DT);
    if(bLevelMode) TickLevel(DT);
#if !UE_BUILD_SHIPPING
    // 可选的艺术截图：等曝光与大气渲染稳定后截取一次。
    static bool bArtCaptured=false;
    if(!bArtCaptured && GetWorld()->GetTimeSeconds()>6.f && FParse::Param(FCommandLine::Get(),TEXT("GridboundScreenshot")))
    {
        FScreenshotRequest::RequestScreenshot(TEXT("GridboundArt.png"),true,false);
        bArtCaptured=true;
    }
#endif
    auto* PC=Cast<APlayerController>(GetController()); if(!PC) return;
    if(PC->WasInputKeyJustPressed(EKeys::F5)) ResetArena();
    if(Health<=0) return;
    if(bLevelMode && PC->WasInputKeyJustPressed(EKeys::F)) InteractLevel();
    float MX,MY; PC->GetInputMouseDelta(MX,MY);
    FRotator Rot=PC->GetControlRotation(); Rot.Yaw+=MX*MouseSensitivity; Rot.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch)+MY*MouseSensitivity,-85.f,85.f);
    PC->SetControlRotation(Rot);
    Body->SetWorldRotation(FRotator(0,Rot.Yaw,0));
    if(Adventurer) Adventurer->SetWorldRotation(FRotator(0,Rot.Yaw,0));
    for(float& Cooldown:Cooldowns) Cooldown=FMath::Max(0.f,Cooldown-DT);
    if(PC->WasInputKeyJustPressed(EKeys::SpaceBar)) TryJump();
    const auto Active=[PC](FKey Key) { return PC->IsInputKeyDown(Key) || PC->WasInputKeyJustPressed(Key); };
    const FIntPoint Input=GridRules::MovementInput(Active(EKeys::W),Active(EKeys::S),Active(EKeys::A),Active(EKeys::D));
    const bool bHasHeldInput=Active(EKeys::W)||Active(EKeys::S)||Active(EKeys::A)||Active(EKeys::D);
    const bool bNewMovePress=PC->WasInputKeyJustPressed(EKeys::W)||PC->WasInputKeyJustPressed(EKeys::S)
        ||PC->WasInputKeyJustPressed(EKeys::A)||PC->WasInputKeyJustPressed(EKeys::D);
    ProcessMovementInput(DT,Input,bHasHeldInput,bNewMovePress,Rot.Yaw);
    if(PC->WasInputKeyJustPressed(EKeys::Q)) SelectedSkill=0;
    if(PC->WasInputKeyJustPressed(EKeys::E)) SelectedSkill=1;
    if(PC->WasInputKeyJustPressed(EKeys::R)) SelectedSkill=2;
    if(SelectedSkill==1 && PC->WasInputKeyJustPressed(EKeys::RightMouseButton)) bWallAlongX=!bWallAlongX;
    UpdateAim();
    if(PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) CastSkill(SelectedSkill);
    DrawCell(CurrentCell,FColor::Cyan);
    if(bHasAim && SelectedSkill==1)
    {
        for(const FIntPoint C:GridRules::WallCells(AimCell,bWallAlongX))
        {
            const FColor Color=WallPlan.Contains(C)?FColor::Green:FColor::Red;
            DrawCell(C,Color);
            DrawDebugBox(GetWorld(),GridRules::Center(C,GridRules::WallHeight*.5f),
                FVector(GridRules::CellSize*.5f,GridRules::CellSize*.5f,GridRules::WallHeight*.5f),Color,false,0.f,0,1.f);
        }
    }
    if(bHasAim && SelectedSkill==2)
        for(const FIntPoint C:GridRules::RainCells(AimCell)) if(GridRules::Inside(C)) DrawCell(C,bValidAim?FColor::Cyan:FColor::Red);
}

void AGridHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* P=Cast<AGridPawn>(GetOwningPawn()); if(!P || !Canvas) return;
    if(!ChineseFont)
    {
        ChineseFont=NewObject<UFont>(this);
        ChineseFont->FontCacheType=EFontCacheType::Runtime;
        ChineseFont->LegacyFontSize=18;
        ChineseFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
            FName(TEXT("Regular")),FPaths::ProjectContentDir()/TEXT("Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad));
    }
    const float W=Canvas->SizeX,H=Canvas->SizeY;
    const float S=FMath::Min(W/1280.f,H/720.f);
    auto Text=[&](const FString& Value,float X,float Y,FLinearColor Color=FLinearColor::White)
    { DrawText(Value,Color,X*S,Y*S,ChineseFont,S*.82f); };
    DrawRect(FLinearColor(.015f,.025f,.035f,.72f),20*S,20*S,770*S,208*S);
    Text(TEXT("玩法介绍"),34,28,FLinearColor(.5f,1,.8f));
    Text(TEXT("WASD 移动　空格跳跃　Q 火球术　E 土墙术　R 降雨术"),34,56);
    const FString Selected=P->SelectedSkill==0?TEXT("火球术"):P->SelectedSkill==1?TEXT("土墙术"):P->SelectedSkill==2?TEXT("降雨术"):TEXT("未选择");
    Text(FString::Printf(TEXT("左键施放，右键旋转土墙，F5 重来；当前选择：%s"),*Selected),34,82);
    Text(TEXT("火球点燃草地；降雨灭火并制造减速泥地；土墙隔敌、登高。"),34,108);
    Text(P->bLevelMode?TEXT("河道有两处渡口：草桥可用火封路，远处砂石路可用雨减速追兵。"):
        TEXT("泥地也会减慢自己；土墙会被破坏，施法前为自己留好退路。"),34,134);
    Text(TEXT("绕岩石可切断敌人视线；看到橙色攻击格及时移开，可躲过劈砍。"),34,160);
    Text(P->bLevelMode?P->LevelObjective():TEXT("自由练习技能联动，按 F5 重新开始。"),34,194,FLinearColor(1,.85f,.4f));
    DrawRect(FLinearColor(.015f,.025f,.035f,.8f),20*S,H-68*S,280*S,48*S);
    DrawRect(FLinearColor(.15f,.06f,.06f),34*S,H-36*S,250*S,8*S);
    DrawRect(FLinearColor(.2f,.85f,.45f),34*S,H-36*S,250*S*P->Health/100.f,8*S);
    DrawText(FString::Printf(TEXT("生命值　%d / 100"),P->Health),FLinearColor::White,34*S,H-63*S,ChineseFont,S*.82f);
    // 右上角的技能冷却槽，让玩家无需猜读秒就能规划施法。
    {
        const FString Names[3]={TEXT("火球术"),TEXT("土墙术"),TEXT("降雨术")};
        const float MaxCD[3]={GridRules::FireballCooldown,GridRules::WallCooldown,GridRules::RainCooldown};
        const float SlotW=140.f,SlotH=44.f,Gap=8.f,Margin=20.f;
        const float StartX=1280.f-(3.f*SlotW+2.f*Gap)-Margin,Y=24.f;
        for(int32 I=0;I<3;++I)
        {
            const float X=StartX+I*(SlotW+Gap);
            const bool bReady=P->Cooldowns[I]<=0.f;
            DrawRect(FLinearColor(.02f,.03f,.05f,.85f),X*S,Y*S,SlotW*S,SlotH*S);
            if(P->SelectedSkill==I) DrawRect(FLinearColor(.5f,1.f,.7f,.9f),X*S,Y*S,SlotW*S,3.f*S);
            const float Fill=bReady?0.f:FMath::Clamp(P->Cooldowns[I]/MaxCD[I],0.f,1.f);
            if(Fill>0.f) DrawRect(FLinearColor(.06f,.06f,.14f,.85f),X*S,(Y+SlotH*(1.f-Fill))*S,SlotW*S,SlotH*Fill*S);
            Text(Names[I],X+8,Y+6,FLinearColor(.95f,.95f,.95f));
            Text(bReady?TEXT("就绪"):FString::Printf(TEXT("%.1f 秒"),P->Cooldowns[I]),X+8,Y+22,bReady?FLinearColor(.4f,1.f,.6f):FLinearColor(1.f,.72f,.3f));
        }
    }
    // 屏幕中心的准星与法术摆放几何，是瞄准的可视化交互提示。
    DrawRect(P->bValidAim?FLinearColor(.5f,1,.7f):FLinearColor::White,W*.5f-2,H*.5f-2,4,4);
    for(const auto& T:P->Targets)
    {
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        FCollisionQueryParams Params; Params.AddIgnoredActor(P); Params.AddIgnoredActor(T.Actor.Get());
        FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,P->Camera->GetComponentLocation(),T.Actor->GetActorLocation(),ECC_Visibility,Params)) continue;
        FVector2D Screen;
        if(PlayerOwner->ProjectWorldLocationToScreen(T.Actor->GetActorLocation()+FVector(0,0,115),Screen))
        {
            // 敌人血条不能盖住左上角的说明或左下角的玩家血条。
            if(Screen.X-30*S<790*S && Screen.X+30*S>20*S && Screen.Y<228*S) continue;
            if(Screen.X-30*S<300*S && Screen.Y+6*S>H-68*S) continue;
            DrawRect(FLinearColor(.06f,.02f,.02f,.9f),Screen.X-30*S,Screen.Y,60*S,6*S);
            DrawRect(FLinearColor(.9f,.16f,.08f),Screen.X-30*S,Screen.Y,60*S*T.Health/100.f,6*S);
        }
    }
    // 全屏色罩给出即时的受伤/燃烧反馈，无需额外 UI 贴图。
    if(P->DamageFlash>0.f) DrawRect(FLinearColor(.85f,.04f,.04f,FMath::Min(P->DamageFlash,1.f)*.32f),0,0,W,H);
    if(P->BurnOverlay>0.f) DrawRect(FLinearColor(1.f,.45f,.05f,FMath::Min(P->BurnOverlay,1.f)*.22f),0,0,W,H);
}
