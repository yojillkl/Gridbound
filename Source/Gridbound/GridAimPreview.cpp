#include "GridGame.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

void AGridPawn::DrawSkillPreview()
{
    if(SelectedSkill<0 || SelectedSkill>=4 || Health<=0 || bLevelComplete) return;
    const FColor Colors[]={FColor(255,150,70),FColor(80,210,255),FColor(140,230,255),FColor(245,115,200)};
    const FColor Color=(!bValidAim || !HasSpellSlot(SelectedSkill))?FColor(255,85,80):
        (SelectedSkill==1?ReactionRemaining:ActionRemaining)>0?FColor(245,180,65):Colors[SelectedSkill];
    if(SelectedSkill==0)
    {
        DrawDebugLine(GetWorld(),SpellOrigin()+AimDirection*40.f,AimPoint,Color,false,0,0,1.f);
        DrawDebugSphere(GetWorld(),AimPoint,GridRules::FireballBlastRadius,32,Color,false,0,0,1.f);
        DrawDebugPoint(GetWorld(),AimPoint,8,Color,false,0);
    }
    else if(SelectedSkill==2)
    {
        const FVector Center=GetActorLocation()+AimDirection*GridRules::ThunderSize*.5f
            +FVector(0,0,GridRules::ThunderSize*.5f-GridRules::ActorOriginHeight);
        DrawDebugBox(GetWorld(),Center,FVector(GridRules::ThunderSize*.5f),AimDirection.Rotation().Quaternion(),Color,false,0,0,2.f);
    }
    if(SelectedSkill==2 || SelectedSkill==3)
        for(int32 I:AimTrace.Enemies) if(Targets.IsValidIndex(I) && Targets[I].Actor.IsValid())
            DrawDebugCapsule(GetWorld(),Targets[I].Actor->GetActorLocation(),85,42,FQuat::Identity,Color,false,0,0,2.f);
}
