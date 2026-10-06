#include "GridArt.h"
#include "Components/SceneComponent.h"

namespace
{
    float Ease(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*(3.f-2.f*T); }
    struct FHandPose { FVector Position=FVector::ZeroVector; FRotator Rotation=FRotator::ZeroRotator; };
    FHandPose Blend(FHandPose A,FHandPose B,float T)
    { return {FMath::Lerp(A.Position,B.Position,T),FMath::Lerp(A.Rotation,B.Rotation,T)}; }
}

float GridArt::CastingDuration(int32 Skill)
{
    const float Durations[]={.56f,.78f,.95f,.62f};
    return Skill>=0 && Skill<4?Durations[Skill]:0.f;
}

void GridArt::AnimateCastingHands(USceneComponent* Root,int32 SelectedSkill,int32 CastingSkill,float Remaining,float Phase,float Speed,float Flinch,float DeltaSeconds,float WindupProgress)
{
    if(!Root) return;
    const bool bCasting=Remaining>0.f && CastingSkill>=0 && CastingSkill<4;
    const bool bWinding=WindupProgress>=0.f && CastingSkill>=0 && CastingSkill<4;
    const int32 Skill=bCasting || bWinding?CastingSkill:SelectedSkill;
    const float Progress=bCasting?FMath::Clamp(1.f-Remaining/CastingDuration(Skill),0.f,1.f):1.f;
    const float Bob=FMath::Sin(Phase*9.f)*.6f*FMath::Clamp(Speed,0.f,1.f);
    Root->SetRelativeLocation(FVector(52,0,-24+Bob));
    Root->SetRelativeRotation(FRotator(0,0,Flinch*4));
    for(USceneComponent* Hand:Root->GetAttachChildren())
    {
        const bool Left=Hand->ComponentHasTag(TEXT("GridHandL"));
        if(!Left && !Hand->ComponentHasTag(TEXT("GridHandR"))) continue;
        const float Side=Left?-1.f:1.f;
        const FVector Rest(Left?0:3,Side*25,Left?0:2);
        FHandPose Ready,Release,Follow;
        switch(Skill)
        {
        case 0: // Cup the flame with the left hand, throw with the right.
            Ready=Left?FHandPose{FVector(1,5,0),FRotator(-12,0,18)}:FHandPose{FVector(-4,-2,-2),FRotator(15,-8,-12)};
            Release=Left?FHandPose{FVector(3,7,2),FRotator(-25,8,30)}:FHandPose{FVector(18,-10,8),FRotator(-70,-12,-8)};
            Follow=Left?Ready:FHandPose{FVector(9,-7,4),FRotator(-42,-8,-5)};
            break;
        case 1: // Raise a close defensive ward.
            Ready={FVector(-2,-Side*2,-3),FRotator(12,Side*8,-Side*18)};
            Release={FVector(12,-Side*9,8),FRotator(-65,Side*14,-Side*25)};
            Follow={FVector(10,Side*3,6),FRotator(-50,Side*22,-Side*20)};
            break;
        case 2: // Push both palms forward with the thunder front.
            Ready={FVector(0,-Side*3,3),FRotator(-8,-Side*12,Side*15)};
            Release={FVector(20,-Side*3,5),FRotator(-78,Side*12,Side*10)};
            Follow={FVector(12,Side*6,3),FRotator(-45,Side*20,Side*25)};
            break;
        case 3: // Pin the target with an asymmetric binding gesture.
            Ready=Left?FHandPose{FVector(2,5,-2),FRotator(-15,-20,35)}:FHandPose{FVector(2,-4,3),FRotator(-30,5,-12)};
            Release=Left?FHandPose{FVector(6,9,0),FRotator(-35,-25,50)}:FHandPose{FVector(20,-8,8),FRotator(-85,0,-8)};
            Follow=Left?Ready:FHandPose{FVector(12,-6,5),FRotator(-60,0,-5)};
            break;
        default: break;
        }
        // Anticipation precedes gameplay release; the existing release animation starts only at completion.
        FHandPose Pose=Ready;
        if(bWinding) Pose=Blend(Ready,{Ready.Position+FVector(-5,0,6),Ready.Rotation+FRotator(15,0,0)},Ease(WindupProgress));
        else if(bCasting) Pose=Progress<.32f?Blend(Release,Follow,Ease(Progress/.32f)):
            Blend(Follow,Ready,Ease((Progress-.32f)/.68f));
        const float PrepareBlend=!bCasting && DeltaSeconds>0.f?1.f-FMath::Exp(-14.f*DeltaSeconds):1.f;
        Hand->SetRelativeLocation(FMath::Lerp(Hand->GetRelativeLocation(),Rest+Pose.Position,PrepareBlend));
        Hand->SetRelativeRotation(FMath::Lerp(Hand->GetRelativeRotation(),Pose.Rotation,PrepareBlend));
    }
    // Keep each sleeve connected to an elbow below the frame instead of rotating its open end into view.
    for(USceneComponent* Sleeve:Root->GetAttachChildren())
    {
        const bool Left=Sleeve->ComponentHasTag(TEXT("GridSleeveL"));
        if(!Left && !Sleeve->ComponentHasTag(TEXT("GridSleeveR"))) continue;
        for(USceneComponent* Hand:Root->GetAttachChildren()) if(Hand->ComponentHasTag(Left?TEXT("GridHandL"):TEXT("GridHandR"))) {
            const FVector Elbow(-22,Left?-32.f:32.f,-38);
            const FVector Direction=Hand->GetRelativeLocation()-Elbow;
            Sleeve->SetRelativeLocation(Elbow);
            Sleeve->SetRelativeRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
            Sleeve->SetRelativeScale3D(FVector(1,1,Direction.Size()/20.f));
        }
    }
}

void GridArt::AnimateWarriorSkill(USceneComponent* Root,int32 Skill,float Windup,float Release,bool bCharging,int32 StrikeNumber)
{
    if(!Root || (Windup<0 && Release<0 && !bCharging)) return;
    const bool Recover=Release>=0;
    const float W=Ease(FMath::Max(0.f,Windup));
    const float Weight=Recover?1.f-Ease(Release):1.f;
    const float Strike=Windup<0?1.f:Ease((Windup-.72f)/.28f);
    FRotator Body=FRotator::ZeroRotator,Left(-10,0,-8),Right(-20,0,8),Head=FRotator::ZeroRotator;
    float Lower=0;
    switch(Skill)
    {
    case 1: // Second Wind: brace the chest, breathe, then straighten up.
        Lower=-5.f*FMath::Sin(PI*FMath::Max(0.f,Windup));
        Body=FRotator(6,-5,0); Left=FRotator(-65,30,-35); Right=FRotator(20,10,12); Head=FRotator(-12,0,0);
        break;
    case 3: // Charge: a low planted preparation and a stable forward sword during travel.
        Lower=bCharging || Recover?-7.f:-8.f*W;
        Body=FRotator(bCharging || Recover?-22.f:-16.f*W,0,0);
        Left=FRotator(bCharging?30.f:15.f*W,0,-25); Right=FRotator(-58,8,10); Head=FRotator(10,0,0);
        break;
    case 4: // Trip: bend the knees and sweep at leg height.
        Lower=-10.f; Body=FRotator(-12,FMath::Lerp(-22.f,18.f,Strike),0);
        Left=FRotator(-35,-15,-25); Right=FRotator(-58,FMath::Lerp(65.f,-55.f,Strike),70); Head=FRotator(-8,0,0);
        break;
    case 5: // Pushing Attack: a forward sword drive, not a low sweep.
        Body=FRotator(FMath::Lerp(8.f,-15.f,Strike),0,0);
        Left=FRotator(-45,10,-20); Right=FRotator(FMath::Lerp(-35.f,-85.f,Strike),-10,12);
        break;
    case 2: // Action Surge: alternate diagonal cuts; each hit still uses its existing telegraph.
    {
        const float Side=StrikeNumber%2?-1.f:1.f;
        Body=FRotator(-6,Side*FMath::Lerp(-20.f,22.f,Strike),Side*4);
        Left=FRotator(-28,0,-18);
        Right=FRotator(FMath::Lerp(-95.f,28.f,Strike),Side*FMath::Lerp(35.f,-30.f,Strike),Side*18);
        break;
    }
    default:
        Body=FRotator(-4,FMath::Lerp(-10.f,12.f,Strike),0);
        Right=FRotator(FMath::Lerp(-100.f,25.f,Strike),0,8);
        break;
    }
    // Blend from locomotion early in the windup, and back into it during recovery.
    const float BlendWeight=Weight*(Recover || bCharging?1.f:Ease(Windup/.25f));
    Root->SetRelativeLocation(Root->GetRelativeLocation()+FVector(0,0,Lower*BlendWeight));
    Root->SetRelativeRotation(FMath::Lerp(Root->GetRelativeRotation(),Body,BlendWeight));
    for(USceneComponent* Part:Root->GetAttachChildren())
    {
        const FRotator* Target=Part->ComponentHasTag(TEXT("ArmL"))?&Left:Part->ComponentHasTag(TEXT("ArmR"))?&Right:
            Part->ComponentHasTag(TEXT("Head"))?&Head:nullptr;
        if(Target) Part->SetRelativeRotation(FMath::Lerp(Part->GetRelativeRotation(),*Target,BlendWeight));
        if(Lower<0 && (Part->ComponentHasTag(TEXT("LegL")) || Part->ComponentHasTag(TEXT("LegR")))) {
            Part->SetRelativeRotation(FRotator(-Lower*2*BlendWeight,0,0));
            for(USceneComponent* Knee:Part->GetAttachChildren()) Knee->SetRelativeRotation(FRotator(Lower*3*BlendWeight,0,0));
        }
    }
}
