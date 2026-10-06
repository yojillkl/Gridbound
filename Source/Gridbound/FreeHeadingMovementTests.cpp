#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Components/BoxComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFreeHeadingMovementTest,"Gridbound.Movement.FreeHeading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFreeHeadingMovementTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn=World->SpawnActor<AGridPawn>();
    if(Pawn)
    {
        const FVector Origin(1700,1700,75);
        // 实际调用移动及碰撞路径，而不只检查方向转换公式。
        for(int32 Angle=-180;Angle<180;++Angle)
        {
            Pawn->SetActorLocation(Origin);
            Pawn->MoveContinuous(.1f,FVector2D(1,0),float(Angle));
            const double Radians=Angle*PI/180.;
            const FVector Expected=Origin+FVector(FMath::Cos(Radians),FMath::Sin(Radians),0)*39.;
            TestTrue(FString::Printf(TEXT("W follows camera at %d degrees"),Angle),Pawn->GetActorLocation().Equals(Expected,.001));
        }
        Pawn->SetActorLocation(Origin);
        Pawn->MoveContinuous(.1f,FVector2D(1,1),15.f);
        TestTrue(TEXT("Diagonal remains 390 cm/s"),FMath::IsNearlyEqual(FVector::Dist(Origin,Pawn->GetActorLocation()),39.,.001));
        const FVector Before=Pawn->GetActorLocation();
        Pawn->MoveContinuous(.1f,FVector2D::ZeroVector,15.f);
        TestTrue(TEXT("Released input stops without completing a grid step"),Pawn->GetActorLocation().Equals(Before));
        Pawn->MoveContinuous(.1f,FVector2D(1,0),-15.f);
        TestTrue(TEXT("Turning while moving takes effect immediately"),(Pawn->GetActorLocation()-Before).GetSafeNormal().Equals(FVector(FMath::Cos(PI/12),-FMath::Sin(PI/12),0),.001));
        // 用真实碰撞组件验证滑墙和内角，而非仅检查向量公式。
        auto AddWall=[World](FVector Location,FVector Extent)
        {
            auto* Actor=World->SpawnActor<AActor>();
            auto* Box=NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box); Box->SetBoxExtent(Extent);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent(); Actor->SetActorLocation(Location);
            return Actor;
        };
        AActor* Wall=AddWall(FVector(1800,1700,75),FVector(10,500,500));
        Pawn->SetActorLocation(Origin);
        Pawn->MoveContinuous(.5f,FVector2D(1,1),0);
        TestTrue(TEXT("Diagonal contact slides along wall"),Pawn->GetActorLocation().Y>1820);
        TestTrue(TEXT("Sliding cannot pass through wall"),Pawn->GetActorLocation().X<=1790.f-GridRules::CharacterMoveRadius+.1f);
        AActor* Corner=AddWall(FVector(1700,1950,75),FVector(500,10,500));
        Pawn->MoveContinuous(.5f,FVector2D(1,1),0);
        TestTrue(TEXT("Inside corner blocks both walls"),Pawn->GetActorLocation().X<=1790.f-GridRules::CharacterMoveRadius+.1f
            && Pawn->GetActorLocation().Y<=1940.f-GridRules::CharacterMoveRadius+.1f);
        Wall->Destroy(); Corner->Destroy();
        Pawn->SetActorLocation(Origin); Pawn->FootHeight=0; Pawn->FallSpeed=0; Pawn->bJumping=false;
        Pawn->TickJumpInput(.016f);
        TestTrue(TEXT("Grounded jump starts"),Pawn->TryJump());
        TestFalse(TEXT("No midair double jump"),Pawn->TryJump());
        Pawn->JumpBufferRemaining=0;
        // 模拟走出高台：离地但尚未主动起跳。
        Pawn->bJumping=false; Pawn->FootHeight=100; Pawn->FallSpeed=20;
        Pawn->CoyoteRemaining=GridRules::CoyoteSeconds;
        Pawn->TickJumpInput(.06f);
        TestTrue(TEXT("Jump shortly after leaving edge"),Pawn->TryJump());
        Pawn->bJumping=false; Pawn->FootHeight=100; Pawn->FallSpeed=20;
        Pawn->CoyoteRemaining=GridRules::CoyoteSeconds;
        Pawn->TickJumpInput(.13f);
        TestFalse(TEXT("Expired edge grace cannot jump"),Pawn->TryJump());
        Pawn->FootHeight=1; Pawn->FallSpeed=100; Pawn->bJumping=true;
        Pawn->UpdateElevation(.02f); Pawn->TickJumpInput(.02f);
        TestTrue(TEXT("Buffered press jumps upon landing"),Pawn->bJumping && Pawn->FallSpeed<0);
        Pawn->TryJump(); Pawn->TickJumpInput(.16f);
        Pawn->FootHeight=1; Pawn->FallSpeed=100; Pawn->bJumping=true;
        Pawn->UpdateElevation(.02f); Pawn->TickJumpInput(.02f);
        TestFalse(TEXT("Expired buffer does not jump upon landing"),Pawn->bJumping);
        Pawn->JumpBufferRemaining=.1f; Pawn->CoyoteRemaining=.1f;
        Pawn->RespawnPlayer(FIntPoint(3,10));
        TestTrue(TEXT("Respawn clears pending jump"),Pawn->JumpBufferRemaining==0 && Pawn->CoyoteRemaining==0);
    }
    else AddError(TEXT("Could not spawn pawn"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
