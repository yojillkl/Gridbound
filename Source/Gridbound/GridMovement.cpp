#include "GridGame.h"

void AGridPawn::ProcessMovementInput(float DeltaSeconds,FIntPoint Input,bool bHasHeldInput,bool bNewPress,float Yaw)
{
    if(Health<=0) return;
    if(bNewPress) PendingMoveInput=Input;
    if(bMoving)
    {
        AdvanceMovement(DeltaSeconds);
        if(bMoving) return;
        // Continuous walking and queued turns do not pay the startup chord delay again.
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
    // Keyboard events that feel simultaneous may arrive in neighbouring render frames.
    // Wait only at a single-key startup; a completed chord or released tap commits immediately.
    if(!bCombined && bHasHeldInput && MoveChordAge<GridRules::MovementChordWindow) return;
    const FIntPoint StartInput=PendingMoveInput;
    PendingMoveInput=FIntPoint::ZeroValue; bWaitingForMoveChord=false; MoveChordAge=0.f;
    TryStartMove(StartInput,Yaw);
}

bool AGridPawn::IsGrounded() const
{
    return !bJumping && FMath::IsNearlyEqual(FootHeight,SurfaceHeight(GridRules::Cell(GetActorLocation())),.1f)
        && FMath::IsNearlyZero(FallSpeed);
}

bool AGridPawn::TryJump()
{
    if(Health<=0 || !IsGrounded()) return false;
    FallSpeed=-GridRules::JumpSpeed; bJumping=true;
    return true;
}

bool AGridPawn::CanStep(FIntPoint Step) const
{
    if(Step==FIntPoint::ZeroValue || FMath::Abs(Step.X)>1 || FMath::Abs(Step.Y)>1) return false;
    if(!CanEnter(CurrentCell+Step)) return false;
    // Both side cells must be open: never squeeze through the corner of a pillar or enemy.
    if(Step.X!=0 && Step.Y!=0)
        return CanEnter(CurrentCell+FIntPoint(Step.X,0)) && CanEnter(CurrentCell+FIntPoint(0,Step.Y));
    return true;
}

bool AGridPawn::TryStartMove(FIntPoint Input,float Yaw)
{
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
    // Falling below a wall's top during a step must not allow horizontal penetration.
    if(!CanStep(Step))
    {
        bMoving=false; MoveTime=0.f;
        SetActorLocation(GridRules::Center(CurrentCell,FootHeight+75));
        Destination=CurrentCell;
        return;
    }
    const float DistanceScale=FMath::Sqrt(float(Step.X*Step.X+Step.Y*Step.Y));
    const float Duration=GridRules::StepSeconds*DistanceScale;
    MoveTime+=FMath::Max(0.f,DeltaSeconds)*MovementSpeedMultiplier(CurrentCell,FootHeight);
    const float Alpha=FMath::Clamp(MoveTime/Duration,0.f,1.f);
    SetActorLocation(FMath::Lerp(GridRules::Center(CurrentCell,FootHeight+75),GridRules::Center(Destination,FootHeight+75),Alpha));
    if(Alpha>=1.f) { CurrentCell=Destination; bMoving=false; }
}
