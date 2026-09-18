#include "GridGame.h"

// 网格移动：输入合并（把相邻帧的两次按键合成斜向）、跳跃、步进与落地。
// 移动始终按格推进，跳跃只改变垂直高度，两者可同时进行。

void AGridPawn::ProcessMovementInput(float DeltaSeconds,FIntPoint Input,bool bHasHeldInput,bool bNewPress,float Yaw)
{
    // 输入入口：用一小段缓冲窗口把「几乎同时」的两个方向键合并成一次斜向步进。
    if(Health<=0) return;
    if(bNewPress) PendingMoveInput=Input;
    if(bMoving)
    {
        AdvanceMovement(DeltaSeconds);
        if(bMoving) return;
        // 连续行走和排队的转向不需要再付一次起步缓冲延迟。
        bWaitingForMoveChord=false; MoveChordAge=0.f;
        const FIntPoint Next=bHasHeldInput?Input:PendingMoveInput;
        PendingMoveInput=FIntPoint::ZeroValue;
        TryStartMove(Next,Yaw);
        return;
    }
    if(!bWaitingForMoveChord)
    {
        PendingMoveInput=bHasHeldInput?Input:PendingMoveInput;
        if(PendingMoveInput==FIntPoint::ZeroValue) return;
        bWaitingForMoveChord=true; MoveChordAge=0.f;
    }
    else
    {
        MoveChordAge+=FMath::Max(0.f,DeltaSeconds);
        if(bHasHeldInput) PendingMoveInput=Input;
    }
    if(PendingMoveInput==FIntPoint::ZeroValue)
    {
        bWaitingForMoveChord=false; MoveChordAge=0.f;
        return;
    }
    const bool bCombined=PendingMoveInput.X!=0 && PendingMoveInput.Y!=0;
    // 感觉同时按下的两个键，事件可能落在相邻的两帧里。
    // 只在「单键起步」时等待；已经成对的斜向或松开的单点立即提交。
    if(!bCombined && bHasHeldInput && MoveChordAge<GridRules::MovementChordWindow) return;
    const FIntPoint StartInput=PendingMoveInput;
    PendingMoveInput=FIntPoint::ZeroValue; bWaitingForMoveChord=false; MoveChordAge=0.f;
    TryStartMove(StartInput,Yaw);
}

bool AGridPawn::IsGrounded() const
{
    // 贴地：不在跳跃、脚底与地表基本平齐、且没有垂直速度。
    return !bJumping && FMath::IsNearlyEqual(FootHeight,SurfaceHeight(GridRules::Cell(GetActorLocation())),.1f)
        && FMath::IsNearlyZero(FallSpeed);
}

bool AGridPawn::TryJump()
{
    // 起跳：只有贴地才能跳，直接赋给向上的初速度。
    if(Health<=0 || !IsGrounded()) return false;
    FallSpeed=-GridRules::JumpSpeed; bJumping=true;
    return true;
}

bool AGridPawn::CanStep(FIntPoint Step) const
{
    // 某一步能否迈出：方向合法、目标可进，且斜向时两侧也不能被卡（禁止从柱子/敌人角落挤过）。
    if(Step==FIntPoint::ZeroValue || FMath::Abs(Step.X)>1 || FMath::Abs(Step.Y)>1) return false;
    if(!CanEnter(CurrentCell+Step)) return false;
    if(Step.X!=0 && Step.Y!=0)
        return CanEnter(CurrentCell+FIntPoint(Step.X,0)) && CanEnter(CurrentCell+FIntPoint(0,Step.Y));
    return true;
}

bool AGridPawn::TryStartMove(FIntPoint Input,float Yaw)
{
    // 把输入转成网格方向，若可走则锁定本次步进。
    if(bMoving || Health<=0) return false;
    const FIntPoint Step=GridRules::MoveDirection(Yaw,Input);
    if(!CanStep(Step)) return false;
    Destination=CurrentCell+Step; MoveTime=0.f; bMoving=true;
    return true;
}

void AGridPawn::AdvanceMovement(float DeltaSeconds)
{
    if(!bMoving) return;
    const FIntPoint Step=Destination-CurrentCell;
    // 步进途中若落到土墙顶以下，不能再水平穿透，直接中止并回正。
    if(!CanStep(Step))
    {
        bMoving=false; MoveTime=0.f;
        SetActorLocation(GridRules::Center(CurrentCell,FootHeight+GridRules::ActorOriginHeight));
        Destination=CurrentCell;
        return;
    }
    const float DistanceScale=FMath::Sqrt(float(Step.X*Step.X+Step.Y*Step.Y));
    const float Duration=GridRules::StepSeconds*DistanceScale;
    MoveTime+=FMath::Max(0.f,DeltaSeconds)*MovementSpeedMultiplier(CurrentCell,FootHeight);
    const float Alpha=FMath::Clamp(MoveTime/Duration,0.f,1.f);
    SetActorLocation(FMath::Lerp(GridRules::Center(CurrentCell,FootHeight+GridRules::ActorOriginHeight),GridRules::Center(Destination,FootHeight+GridRules::ActorOriginHeight),Alpha));
    if(Alpha>=1.f) { CurrentCell=Destination; bMoving=false; }
}
