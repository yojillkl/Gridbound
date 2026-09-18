#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridMovementInputTest,"Gridbound.Movement.StaggeredDiagonalInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridMovementInputTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto Reset=[Pawn]()
    {
        Pawn->ResetArena(); Pawn->CurrentCell=FIntPoint(3,3); Pawn->Destination=Pawn->CurrentCell;
        Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    };
    for(int32 X:{-1,1}) for(int32 Y:{-1,1}) for(bool bForwardFirst:{false,true})
    {
        Reset();
        const FIntPoint First=bForwardFirst?FIntPoint(X,0):FIntPoint(0,Y);
        Pawn->ProcessMovementInput(.016f,First,true,true,0);
        TestFalse(TEXT("First key alone does not lock a cardinal step"),Pawn->bMoving);
        Pawn->ProcessMovementInput(.025f,FIntPoint(X,Y),true,true,0);
        TestTrue(TEXT("Second key starts combined movement"),Pawn->bMoving);
        TestEqual(TEXT("First destination is diagonal for either event order"),Pawn->Destination,FIntPoint(3+X,3+Y));
        TestEqual(TEXT("No preceding cardinal cell was visited"),Pawn->CurrentCell,FIntPoint(3,3));
        Pawn->ProcessMovementInput(.43f,FIntPoint::ZeroValue,false,false,0);
        TestEqual(TEXT("Released chord executes exactly one diagonal step"),Pawn->CurrentCell,FIntPoint(3+X,3+Y));
        TestFalse(TEXT("Released chord does not add another step"),Pawn->bMoving);
    }
    Reset();
    Pawn->ProcessMovementInput(.016f,FIntPoint(1,1),true,true,0);
    TestTrue(TEXT("Same-frame combination starts immediately"),Pawn->bMoving);
    Reset();
    Pawn->ProcessMovementInput(.016f,FIntPoint(1,0),true,true,0);
    Pawn->ProcessMovementInput(.016f,FIntPoint::ZeroValue,false,false,0);
    TestTrue(TEXT("Brief single-key tap is preserved"),Pawn->bMoving);
    Pawn->ProcessMovementInput(.31f,FIntPoint::ZeroValue,false,false,0);
    TestEqual(TEXT("Tap moves exactly one cell"),Pawn->CurrentCell,FIntPoint(4,3));
    TestFalse(TEXT("Tap stops after one cell"),Pawn->bMoving);
    Reset();
    Pawn->ProcessMovementInput(.016f,FIntPoint(1,0),true,true,0);
    Pawn->ProcessMovementInput(.07f,FIntPoint(1,0),true,false,0);
    TestTrue(TEXT("Held single key starts after bounded grace window"),Pawn->bMoving);
    Pawn->ProcessMovementInput(.31f,FIntPoint(1,0),true,false,0);
    TestTrue(TEXT("Held movement has no delay between steps"),Pawn->bMoving);
    TestEqual(TEXT("Second continuous destination"),Pawn->Destination,FIntPoint(5,3));
    Reset();
    Pawn->ProcessMovementInput(.016f,FIntPoint(1,0),true,true,0);
    Pawn->ProcessMovementInput(.016f,FIntPoint::ZeroValue,true,true,0);
    TestFalse(TEXT("Opposite key cancels pending startup"),Pawn->bMoving);
    TestFalse(TEXT("Cancellation clears chord state"),Pawn->bWaitingForMoveChord);
    Reset();
    Pawn->ProcessMovementInput(.016f,FIntPoint(1,0),true,true,0);
    Pawn->ResetArena();
    TestFalse(TEXT("Reset clears pending chord"),Pawn->bWaitingForMoveChord);
    TestEqual(TEXT("Reset clears pending input"),Pawn->PendingMoveInput,FIntPoint::ZeroValue);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
