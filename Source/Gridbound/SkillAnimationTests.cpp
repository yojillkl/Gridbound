#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSkillAnimationTest,"Gridbound.Animation.SkillRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkillAnimationTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    auto* PC=World->SpawnActor<APlayerController>();
    if(P && PC)
    {
        PC->Possess(P);
        P->CastingHands=GridArt::CreateCastingHands(P,P->GetRootComponent(),P->BaseMaterial);
        auto Find=[](USceneComponent* Root,const TCHAR* Tag)->USceneComponent* {
            for(USceneComponent* C:Root->GetAttachChildren()) if(C->ComponentHasTag(Tag)) return C;
            return nullptr;
        };
        auto* Left=Find(P->CastingHands,TEXT("GridHandL"));
        auto* Right=Find(P->CastingHands,TEXT("GridHandR"));
        if(TestNotNull(TEXT("Independent left hand"),Left) && TestNotNull(TEXT("Independent right hand"),Right))
        {
            TArray<FTransform> ReleasePoses;
            for(int32 Skill=0;Skill<4;++Skill)
            {
                const float Duration=GridArt::CastingDuration(Skill);
                GridArt::AnimateCastingHands(P->CastingHands,Skill,Skill,0,0,0,0);
                const FTransform Ready=Right->GetRelativeTransform();
                GridArt::AnimateCastingHands(P->CastingHands,Skill,Skill,Duration,0,0,0);
                const FTransform Released=Right->GetRelativeTransform(); ReleasePoses.Add(Released);
                if(auto* Sleeve=Find(P->CastingHands,TEXT("GridSleeveR")))
                    TestTrue(TEXT("Forearm stays connected to released hand"),
                        Sleeve->GetRelativeTransform().TransformPosition(FVector(0,0,20)).Equals(Right->GetRelativeLocation(),.001f));
                TestFalse(TEXT("Release differs from preparation"),Released.Equals(Ready,.001f));
                GridArt::AnimateCastingHands(P->CastingHands,(Skill+1)%4,Skill,Duration,0,0,0);
                TestTrue(TEXT("Changing selection cannot change an active cast"),Released.Equals(Right->GetRelativeTransform(),.001f));
                GridArt::AnimateCastingHands(P->CastingHands,Skill,Skill,Duration*.55f,0,0,0);
                TestFalse(TEXT("Animation advances after release"),Released.Equals(Right->GetRelativeTransform(),.001f));
                GridArt::AnimateCastingHands(P->CastingHands,Skill,Skill,0,0,0,0);
                TestTrue(TEXT("Recovery returns to prepared pose"),Ready.Equals(Right->GetRelativeTransform(),.001f));
            }
            for(int32 I=0;I<4;++I) for(int32 J=I+1;J<4;++J)
                TestFalse(TEXT("Spells have distinct releases"),ReleasePoses[I].Equals(ReleasePoses[J],.001f));
        }
        P->RespawnPlayer(FIntPoint(2,10)); P->SpawnWarrior(FIntPoint(5,10));
        PC->SetControlRotation((P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).Rotation());
        P->Targets[0].WisdomSave=100;
        P->CastSkill(3);
        P->TickSpellCast(.8f);
        TestEqual(TEXT("Resisted hold starts its release without damage"),P->Targets[0].Health,GridRules::WarriorHealth);
        TestEqual(TEXT("Successful cast records the released skill"),P->CastingSkill,3);
        TestTrue(TEXT("Successful cast begins animation"),P->SpellCastAnimation>0);
        P->SpellCastAnimation=.2f; P->CastSkill(3);
        TestEqual(TEXT("Cooldown failure does not restart animation"),P->SpellCastAnimation,.2f);
        using ESkill=AGridPawn::EWarriorSkill;
        auto& T=P->Targets[0]; T.Skill=ESkill::SecondWind; T.Health=10; T.Windup=0;
        P->ResolveWarriorSkill(0);
        TestTrue(TEXT("Second Wind heals 1d10+5"),T.Health>=16 && T.Health<=25);
        TestTrue(TEXT("Heal recovery survives gameplay skill reset"),T.ReleasedSkill==ESkill::SecondWind && T.SkillRecoveryRemaining>0);
        T.Skill=ESkill::Trip; T.StrikesRemaining=0; T.AttackCooldown=0;
        P->ResolveWarriorSkill(0);
        TestTrue(TEXT("Trip retains its sweep after state transitions"),T.ReleasedSkill==ESkill::Trip && T.SkillRecoveryRemaining>0);
        auto* Arm=Find(T.Visual.Get(),TEXT("ArmR"));
        if(TestNotNull(TEXT("Warrior sword arm"),Arm))
        {
            TArray<FTransform> SkillPoses;
            for(int32 Skill=1;Skill<=4;++Skill) {
                GridArt::AnimateAdventurer(T.Visual.Get(),0,0,0,0,0);
                GridArt::AnimateWarriorSkill(T.Visual.Get(),Skill,.65f,-1,false,0);
                SkillPoses.Add(Arm->GetRelativeTransform());
            }
            for(int32 I=0;I<4;++I) for(int32 J=I+1;J<4;++J)
                TestFalse(TEXT("Fighter skills use distinct sword arm poses"),SkillPoses[I].Equals(SkillPoses[J],.001f));
        }
        T.StaggerRemaining=.2f; P->TickWarriorBrain(0,.01f);
        TestEqual(TEXT("Stagger cancels the skill recovery pose"),T.SkillRecoveryRemaining,0.f);
        P->RespawnPlayer(FIntPoint(2,10));
        TestEqual(TEXT("Respawn clears player animation"),P->SpellCastAnimation,0.f);
        TestEqual(TEXT("Respawn clears cast identity"),P->CastingSkill,INDEX_NONE);
        TestEqual(TEXT("Respawn clears enemy recovery"),T.SkillRecoveryRemaining,0.f);
    }
    else AddError(TEXT("Failed to spawn actors"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
