#include "GridGame.h"
#include "GridArt.h"
#include "Engine/World.h"

// 连续第三人称/第一人称移动：输入直接转换为世界方向，网格坐标只作为旧系统的兼容缓存。

void AGridPawn::MoveContinuous(float DeltaSeconds,FVector2D Input,float Yaw)
{
    if(Input.IsNearlyZero() || DeltaSeconds<=0.f) return;
    const FVector Direction=GridRules::WorldMoveDirection(Yaw,Input);
    if(Direction.IsNearlyZero()) return;
    const float Speed=MovementSpeed*MovementSpeedMultiplier(GridRules::Cell(GetActorLocation()),FootHeight)
        *(TripRemaining>0.f?.35f:1.f)*(Input.X<0.f?GridRules::BackwardSpeedScale:1.f)
        *(PendingCastSkill!=INDEX_NONE?GridRules::CastingSpeedScale:1.f);
    const FVector Start=GetActorLocation();
    FVector End=Start+Direction*Speed*DeltaSeconds;
    End.X=FMath::Clamp(End.X,0.f,GridRules::BoardSize*GridRules::CellSize);
    End.Y=FMath::Clamp(End.Y,0.f,GridRules::BoardSize*GridRules::CellSize);
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    for(const auto& T:Targets) if(T.Actor.IsValid()) Params.AddIgnoredActor(T.Actor.Get());
    FVector Position=Start;
    // 小步检查逻辑地形；连续扫掠负责实体碰撞，避免低帧率跨过窄障碍。
    const FVector Delta=End-Start;
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Delta.Size2D()/24.f));
    const auto Allowed=[this](FVector P)
    {
        const FIntPoint Cell=GridRules::Cell(P);
        return GridRules::Inside(Cell) && (!bLevelMode || bJumping ||
            (GridArt::TerrainAt(Cell)!=GridArt::ETerrain::River && !GridArt::CliffAt(Cell)));
    };
    for(int32 Step=0;Step<Steps;++Step)
    {
        FVector Remaining=Delta/Steps;
        if(!Allowed(Position+Remaining))
        {
            // 河岸等逻辑边界也允许沿边移动。
            const FVector AlongX(Remaining.X,0,0),AlongY(0,Remaining.Y,0);
            if(Allowed(Position+AlongX)) Remaining=AlongX;
            else if(Allowed(Position+AlongY)) Remaining=AlongY;
            else break;
        }
        for(int32 Contact=0;Contact<3 && !Remaining.IsNearlyZero();++Contact)
        {
            if(!Allowed(Position+Remaining)) break;
            FHitResult Hit;
            if(!GetWorld()->SweepSingleByChannel(Hit,Position,Position+Remaining,FQuat::Identity,
                ECC_Visibility,FCollisionShape::MakeSphere(GridRules::CharacterMoveRadius),Params))
            { Position+=Remaining; break; }
            if(Hit.bStartPenetrating) break;
            // 仅移除朝向墙面的分量，保留沿墙速度；再次扫掠防止拐角穿墙。
            const FVector Normal=Hit.Normal.GetSafeNormal2D();
            // 留出 1 毫米间隙，避免浮点误差让下一次切向扫掠误判为陷入墙内。
            Position=Hit.Location+Normal*.1f;
            Remaining*=1.f-Hit.Time;
            Remaining-=Normal*FMath::Min(0.f,FVector::DotProduct(Remaining,Normal));
        }
    }
    SetActorLocation(Position);
    PlayerWorldPosition=GetActorLocation();
    CurrentCell=GridRules::Cell(GetActorLocation());
    Destination=CurrentCell; bMoving=false;
}

bool AGridPawn::IsGrounded() const
{
    // 贴地：不在跳跃、脚底与连续地形高度基本平齐。
    return !bJumping && FMath::IsNearlyEqual(FootHeight,SurfaceHeight(GridRules::Cell(GetActorLocation())),3.f)
        && FMath::IsNearlyZero(FallSpeed);
}

bool AGridPawn::TryJump()
{
    if(TripRemaining>0.f) return false;
    if(Health<=0) { JumpBufferRemaining=0.f; CoyoteRemaining=0.f; return false; }
    JumpBufferRemaining=GridRules::JumpBufferSeconds;
    if(!IsGrounded() && (bJumping || CoyoteRemaining<=0.f)) return false;
    FallSpeed=-GridRules::JumpSpeed; bJumping=true;
    JumpBufferRemaining=0.f; CoyoteRemaining=0.f;
    return true;
}

void AGridPawn::TickJumpInput(float DeltaSeconds)
{
    if(Health<=0) { JumpBufferRemaining=0.f; CoyoteRemaining=0.f; return; }
    const float DT=FMath::Max(0.f,DeltaSeconds);
    JumpBufferRemaining=FMath::Max(0.f,JumpBufferRemaining-DT);
    CoyoteRemaining=IsGrounded()?GridRules::CoyoteSeconds:FMath::Max(0.f,CoyoteRemaining-DT);
    if(JumpBufferRemaining>0.f && IsGrounded()) TryJump();
}
