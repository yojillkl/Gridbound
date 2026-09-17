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
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"

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
    Camera->SetRelativeLocation(FVector(0,0,GridRules::EyeHeight-75.f));
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
    // Non-blocking team-coloured spawn pads.
    for(bool bEnemy:{false,true})
    {
        if(bLevelMode && bEnemy) continue;
        const FIntPoint Cell=bEnemy?EnemySpawnCell:PlayerSpawnCell;
        AActor* Pad=MakeBlock(GridRules::Center(Cell,3),FVector(1.1f,1.1f,.04f),
            bEnemy?FLinearColor(.8f,.12f,.035f):FLinearColor(.025f,.65f,1.f),false);
        Pad->Tags.Add(TEXT("GridSpawnPad"));
    }
    // A soft unlit backdrop also covers the lower horizon outside the finite board.
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
    SetActorLocation(GridRules::Center(CurrentCell,75));
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
    Feedback=TEXT("Q Fireball | E Earth Wall | R Rain | LMB cast | F5 reset");
    if(bLevelMode) ResetLevel();
}

float AGridPawn::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    if(!FMath::IsFinite(DamageAmount) || DamageAmount<=0.f || Health<=0 || (bLevelMode && bLevelComplete)) return 0.f;
    const int32 Applied=FMath::Min(Health,FMath::CeilToInt(FMath::Min(DamageAmount,float(GridRules::MaxHealth))));
    Health-=Applied;
    if(Health==0)
    {
        if(bLevelMode) ++LevelDeaths;
        RespawnRemaining=GridRules::PlayerRespawnDelay;
        bMoving=false; bWaitingForMoveChord=false; PendingMoveInput=FIntPoint::ZeroValue;
        Feedback=TEXT("Defeated. Respawning in 2 seconds...");
    }
    return float(Applied);
}

bool AGridPawn::CanEnter(FIntPoint Cell) const
{
    if(!GridRules::Inside(Cell)) return false;
    if(bLevelMode && LevelBlocked(Cell)) return false;
    if(SurfaceHeight(Cell)>FootHeight+20.f) return false;
    for(const auto& T:Targets) if(T.Health>0 && (T.Cell==Cell || (T.bWalking && T.MoveDestination==Cell))) return false;
    return true;
}

bool AGridPawn::HasLineOfSight(FIntPoint Cell,const AActor* Target) const
{
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
        bHasAim=true;
        bValidAim=!(AimPoint-SpellOrigin()).IsNearlyZero();
        return;
    }
    if(!bHit) return;
    for(int I=0;I<Targets.Num();++I) if(Targets[I].Health>0 && Targets[I].Actor.Get()==Hit.GetActor()) AimEnemy=I;
    if(AimEnemy!=INDEX_NONE) AimCell=Targets[AimEnemy].Cell;
    else
    {
        // Existing pillars can be aimed at: the blocked cell is omitted from wall placement.
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
    bValidAim=SelectedSkill==1?CanPlaceWall(AimCell):CanCastRain(AimCell);
}

void AGridPawn::CastSkill(int32 Skill)
{
    if(Skill<0 || Skill>2 || Health<=0) return;
    SelectedSkill=Skill; UpdateAim();
    if(Cooldowns[Skill]>0) { Feedback=TEXT("Skill cooling down"); return; }
    if(!bValidAim) { Feedback=TEXT("Invalid placement: check range, occupied cells and ground"); return; }
    if(Skill==0)
    {
        LaunchFireball((AimPoint-SpellOrigin()).GetSafeNormal());
        Feedback=TEXT("Fireball: 50 character damage / 50 demolition");
    }
    else if(Skill==1)
    {
        const int32 Before=Walls.Num();
        if(!PlaceWall(AimCell)) { Feedback=TEXT("Earth Wall placement failed"); return; }
        Feedback=FString::Printf(TEXT("Earth Wall: %d / 5 pillars raised"),Walls.Num()-Before);
    }
    else
    {
        if(!CastRain(AimCell)) { Feedback=TEXT("Rain target is out of range or blocked"); return; }
        Feedback=TEXT("Rain extinguishes fire / mud slows movement by 50% for 60s");
    }
    Cooldowns[Skill]=Skill==0?.6f:Skill==1?2.f:GridRules::RainCooldown;
    if(bLevelMode) ++LevelCasts[Skill];
}

void AGridPawn::LaunchFireball(FVector Direction)
{
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
    // Compute the whole plan before spawning; new pillars must not obstruct their siblings.
    const auto Cells=PlaceableWallCells(CenterCell);
    if(Cells.IsEmpty()) return false;
    if(bMoving && (Cells.Contains(CurrentCell) || Cells.Contains(Destination)))
    {
        CurrentCell=GridRules::Cell(GetActorLocation()); Destination=CurrentCell;
        bMoving=false; MoveTime=0; PendingMoveInput=FIntPoint::ZeroValue;
        bWaitingForMoveChord=false; MoveChordAge=0.f;
        SetActorLocation(GridRules::Center(CurrentCell,FootHeight+75));
    }
    for(auto& T:Targets) if(T.Health>0 && T.bWalking && T.Actor.IsValid()
        && (Cells.Contains(T.Cell) || Cells.Contains(T.MoveDestination)))
    {
        T.Cell=GridRules::Cell(T.Actor->GetActorLocation()); T.MoveDestination=T.Cell;
        T.bWalking=false; T.MoveProgress=0;
        T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+75));
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
    TickWalls(DT);
    UpdateElevation(DT);
    TickBurning(DT);
    TickWetTerrain(DT);
    TickFireballs(DT);
    TickCombatants(DT);
    if(bLevelMode) TickLevel(DT);
#if !UE_BUILD_SHIPPING
    // Opt-in capture after exposure and atmospheric rendering have settled.
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
        const auto Allowed=PlaceableWallCells(AimCell);
        for(const FIntPoint C:GridRules::WallCells(AimCell,bWallAlongX))
        {
            const FColor Color=Allowed.Contains(C)?FColor::Green:FColor::Red;
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
    const float W=Canvas->SizeX,H=Canvas->SizeY;
    DrawRect(FLinearColor(0.015f,0.025f,0.045f,0.85f),20,20,680,190);
    DrawText(TEXT("GRIDBOUND v0.2.0  /  FIRST-PERSON SPELLCASTING"),FLinearColor(0.25f,0.85f,1),36,32,nullptr,1.3f);
    DrawText(TEXT("WASD 8-way | Space jump | F5 reset | RMB rotate wall"),FLinearColor::White,36,63);
    DrawText(TEXT("Q Fireball | E Earth Wall | R Rain | LMB cast"),FLinearColor::White,36,85);
    DrawText(FString::Printf(TEXT("Fireball %.1fs | Wall %.1fs | Rain %.1fs | Speed %.0f%%"),P->Cooldowns[0],P->Cooldowns[1],P->Cooldowns[2],100*P->MovementSpeedMultiplier(P->CurrentCell,P->FootHeight)),FLinearColor(0.65f,0.8f,0.9f),36,110);
    DrawText(P->Feedback,FLinearColor(1,0.8f,0.35f),36,140);
    if(P->bLevelMode)
    {
        DrawRect(FLinearColor(.015f,.025f,.04f,.88f),20,H-108,850,88);
        DrawText(P->bLevelComplete?TEXT("ELEMENTAL CROSSING  /  COMPLETE"):TEXT("ELEMENTAL CROSSING  /  F interact with beacon"),FLinearColor(.45f,1,.7f),36,H-98,nullptr,1.1f);
        DrawText(P->LevelObjective(),FLinearColor::White,36,H-72);
        DrawText(FString::Printf(TEXT("Time %.0fs | Deaths %d | Q %d  E %d  R %d | F5 restart"),P->LevelSeconds,P->LevelDeaths,P->LevelCasts[0],P->LevelCasts[1],P->LevelCasts[2]),FLinearColor(.7f,.85f,.9f),36,H-46);
        const float MapX=W-194,MapY=24,Size=8;
        DrawRect(FLinearColor(.02f,.03f,.04f,.9f),MapX-6,MapY-6,172,194);
        for(int32 X=0;X<20;++X) for(int32 Y=0;Y<20;++Y)
        {
            const FIntPoint Cell(X,Y);
            FLinearColor Color=GridArt::TerrainAt(Cell)==GridArt::ETerrain::River?FLinearColor(.1f,.35f,.6f):FLinearColor(.24f,.38f,.2f);
            if(P->LevelBlocked(Cell)) Color=GridArt::TerrainAt(Cell)==GridArt::ETerrain::River?Color:FLinearColor(.15f,.18f,.2f);
            if(P->SurfaceHeight(Cell)>0 && !P->LevelBlocked(Cell)) Color=FLinearColor(.52f,.4f,.2f);
            DrawRect(Color,MapX+X*Size,MapY+Y*Size,7,7);
        }
        const FIntPoint Goals[]={FIntPoint(5,10),FIntPoint(12,10),FIntPoint(18,10)};
        for(int32 I=0;I<3;++I) DrawRect(I<P->LevelStage?FLinearColor::Green:FLinearColor(1,.75f,.1f),MapX+Goals[I].X*Size,MapY+Goals[I].Y*Size,7,7);
        for(const auto& T:P->Targets) if(T.Health>0) DrawRect(FLinearColor::Red,MapX+T.Cell.X*Size,MapY+T.Cell.Y*Size,7,7);
        const FIntPoint Position=GridRules::Cell(P->GetActorLocation());
        DrawRect(FLinearColor(0,1,1),MapX+Position.X*Size,MapY+Position.Y*Size,7,7);
        DrawText(TEXT("YOU cyan | GOAL gold"),FLinearColor::White,MapX,MapY+166);
    }
    DrawRect(FLinearColor(.12f,.04f,.04f),36,174,200,12);
    DrawRect(FLinearColor(.15f,.8f,.35f),36,174,200.f*P->Health/GridRules::MaxHealth,12);
    DrawText(FString::Printf(TEXT("HP %d / %d"),P->Health,GridRules::MaxHealth),FLinearColor::White,250,170);
    const FLinearColor C=P->bValidAim?FLinearColor::Green:FLinearColor::White;
    DrawLine(W/2-9,H/2,W/2-3,H/2,C,2); DrawLine(W/2+3,H/2,W/2+9,H/2,C,2);
    DrawLine(W/2,H/2-9,W/2,H/2-3,C,2); DrawLine(W/2,H/2+3,W/2,H/2+9,C,2);
    const FString Spell=P->SelectedSkill==0?TEXT("FIREBALL / 50 DMG / 50 demolition"):
        P->SelectedSkill==1?FString::Printf(TEXT("WALL / range 10 / %s / %d of 5 pillars"),P->bWallAlongX?TEXT("5 x 1"):TEXT("1 x 5"),P->bHasAim?P->PlaceableWallCells(P->AimCell).Num():0):
        P->SelectedSkill==2?TEXT("RAIN / radius 4 / range 10 / mud 60s"):TEXT("Q / E / R to select a spell");
    DrawText(Spell,C,W/2+16,H/2+16);
    for(const auto& T:P->Targets)
    {
        if(T.Health<=0) continue;
        FVector2D Screen;
        if(T.Actor.IsValid() && PlayerOwner->ProjectWorldLocationToScreen(T.Actor->GetActorLocation()+FVector(0,0,115),Screen))
        {
            DrawRect(FLinearColor(0.06f,0.02f,0.02f,0.9f),Screen.X-36,Screen.Y,72,8);
            DrawRect(FLinearColor(0.9f,0.16f,0.08f),Screen.X-36,Screen.Y,72*T.Health/100.f,8);
            DrawText(FString::Printf(TEXT("%s %d"),T.Windup>0?TEXT("SLASH!"):T.AlertTime>0?TEXT("Warrior"):TEXT("Guard"),T.Health),T.Windup>0?FLinearColor(1,.7f,.1f):FLinearColor::White,Screen.X-35,Screen.Y-20);
        }
    }
    FHitResult WallHit; FCollisionQueryParams Params; Params.AddIgnoredActor(P);
    const FVector Eye=P->Camera->GetComponentLocation();
    if(GetWorld()->LineTraceSingleByChannel(WallHit,Eye,Eye+P->Camera->GetForwardVector()*3000,ECC_Visibility,Params))
        for(const auto& Wall:P->Walls) if(Wall.Actor.Get()==WallHit.GetActor())
            DrawText(FString::Printf(TEXT("PILLAR %d / 100  |  %.0fs"),Wall.Durability,Wall.Remaining),FLinearColor(1,.78f,.4f),W/2-100,H/2+48);
}
