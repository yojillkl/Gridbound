#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "GridGame.generated.h"

class UProceduralMeshComponent;

namespace GridRules
{
    constexpr float CellSize = 150.f;
    constexpr int32 BoardSize = 20;
    constexpr int32 MaxHealth = 100;
    constexpr int32 FireballDamage = 50;
    constexpr int32 FireballDemolition = 50;
    constexpr int32 WallDurability = 100;
    constexpr float WallLifetime = 60.f;
    constexpr float WallRiseTime = .35f;
    constexpr float BurnLifetime = 10.f;
    constexpr float BurnDamagePerSecond = 10.f;
    constexpr float EyeHeight = 130.f;
    constexpr float JumpHeight = 145.f;
    constexpr float JumpApexSeconds = .41f;
    constexpr float JumpSpeed = 2.f * JumpHeight / JumpApexSeconds;
    constexpr float JumpGravity = 2.f * JumpHeight / (JumpApexSeconds * JumpApexSeconds);
    constexpr float Gravity = 980.f;
    constexpr float StepSeconds = .3f;
    constexpr float MovementChordWindow = .06f;
    constexpr int32 SlashDamage = 20;
    constexpr int32 SlashDemolition = 10;
    constexpr float SlashCooldown = 1.f;
    constexpr float WarriorStepSeconds = .45f;
    constexpr float PlayerRespawnDelay = 2.f;
    inline FIntPoint MovementInput(bool Forward, bool Back, bool Left, bool Right)
    { return FIntPoint(int32(Forward)-int32(Back),int32(Right)-int32(Left)); }
    inline FIntPoint MoveDirection(float Yaw, FIntPoint Input)
    {
        if(Input==FIntPoint::ZeroValue) return FIntPoint::ZeroValue;
        const FVector Desired=FRotator(0,Yaw,0).RotateVector(FVector(Input.X,Input.Y,0));
        const float Angle=FMath::RoundToInt(FMath::Atan2(Desired.Y,Desired.X)/(PI/4.f))*(PI/4.f);
        return FIntPoint(FMath::RoundToInt(FMath::Cos(Angle)),FMath::RoundToInt(FMath::Sin(Angle)));
    }
    constexpr float FireballRange = 6.f * CellSize;
    constexpr float FireballSpeed = 1800.f;
    constexpr float FireballRadius = 18.f;
    constexpr int32 WallRange = 10;
    constexpr int32 RainRadius = 4;
    constexpr int32 RainRange = 10;
    constexpr float RainCooldown = 4.f;
    constexpr float RainVisualLifetime = 3.f;
    constexpr float TerrainRecovery = 60.f;
    inline TArray<FIntPoint> RainCells(FIntPoint CenterCell)
    {
        TArray<FIntPoint> Cells;
        for(int32 X=-RainRadius;X<=RainRadius;++X) for(int32 Y=-RainRadius;Y<=RainRadius;++Y)
            if(X*X+Y*Y<=RainRadius*RainRadius) Cells.Add(CenterCell+FIntPoint(X,Y));
        return Cells;
    }
    constexpr float WallHeight = 3.f * CellSize;
    inline TArray<FIntPoint> WallCells(FIntPoint CenterCell, bool bAlongX)
    {
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
    // No spell is armed until Q, E or R is pressed.
    int32 SelectedSkill = INDEX_NONE;
    bool bWallAlongX = false;
    float Cooldowns[3] = {0.f,0.f,0.f};
    FString Feedback = TEXT("Q Fireball | E Earth Wall | R Rain | LMB cast");
    struct FTarget {
        FIntPoint Cell; int32 Health=100; TWeakObjectPtr<AActor> Actor;
        float FootHeight=0.f; float FallSpeed=0.f; float BurnFraction=0.f;
        FIntPoint MoveDestination=FIntPoint::ZeroValue;
        float MoveProgress=0.f; bool bWalking=false;
        float AttackCooldown=GridRules::SlashCooldown;
        float SlashRemaining=0.f;
        FIntPoint HomeCell=FIntPoint::ZeroValue;
        float AlertTime=0.f;
        float ThinkTime=0.f;
        float Windup=0.f;
        FIntPoint StrikeCell=FIntPoint::ZeroValue;
        bool bStrikeWall=false;
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
    friend class FGridCombatTest;
    friend class FGridEnvironmentTest;
    friend class FGridRainTest;
    friend class FGridMovementTest;
    friend class FGridMovementInputTest;
    friend class FGridWarriorTest;
    friend class FGridLevelTest;
    friend class AGridHUD;
    struct FFireball { TWeakObjectPtr<AActor> Actor; FVector Direction; float Remaining=GridRules::FireballRange; };
    struct FWall { FIntPoint Cell; TWeakObjectPtr<AActor> Actor; int32 Durability=GridRules::WallDurability; float Remaining=GridRules::WallLifetime; float Age=0.f; TWeakObjectPtr<UProceduralMeshComponent> Visual; };
    struct FBurningCell { FIntPoint Cell; float Remaining=GridRules::BurnLifetime; float Age=0.f; TWeakObjectPtr<AActor> Actor; float RecoveryRemaining=GridRules::TerrainRecovery; };
    struct FMuddyCell { FIntPoint Cell; float Remaining=GridRules::TerrainRecovery; TWeakObjectPtr<AActor> Actor; };
    struct FRainEffect { TWeakObjectPtr<AActor> Actor; float Age=0.f; };
    TArray<FFireball> Fireballs;
    TArray<FWall> Walls;
    TArray<FBurningCell> BurningCells;
    TArray<FMuddyCell> MuddyCells;
    TArray<FRainEffect> RainEffects;
    TMap<FIntPoint,TWeakObjectPtr<AActor>> TerrainTiles;
    float FootHeight=0.f;
    float FallSpeed=0.f;
    float BurnFraction=0.f;
    FVector AimPoint = FVector::ZeroVector;
    UPROPERTY() TObjectPtr<class UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<class UStaticMeshComponent> Body;
    UPROPERTY() TObjectPtr<class UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<class UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<class UMaterialInterface> BaseMaterial;
    UPROPERTY() TObjectPtr<class UMaterialInterface> TerrainMaterial;
    UPROPERTY() TObjectPtr<class UProceduralMeshComponent> Adventurer;
    FIntPoint Destination;
    float MoveTime = 0.f;
    bool bMoving = false;
    bool bJumping = false;
    FIntPoint PendingMoveInput = FIntPoint::ZeroValue;
    bool bWaitingForMoveChord = false;
    float MoveChordAge = 0.f;
    float RespawnRemaining = 0.f;
    bool bLevelMode=false;
    int32 LevelStage=0;
    bool bLevelComplete=false;
    bool bFireSeal=false;
    bool bRainSeal=false;
    float LevelSeconds=0.f;
    int32 LevelDeaths=0;
    int32 LevelCasts[3]={0,0,0};
    TArray<TWeakObjectPtr<AActor>> LevelProps;
    TArray<TWeakObjectPtr<AActor>> LevelGates;
    TArray<TWeakObjectPtr<AActor>> LevelBeacons;
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
    bool EnemyCellOccupied(FIntPoint Cell, int32 Self) const;
    bool EnemyNextStep(int32 Index, bool bAllowWalls, FIntPoint& Next) const;
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
};

UCLASS()
class AGridGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AGridGameMode();
};
