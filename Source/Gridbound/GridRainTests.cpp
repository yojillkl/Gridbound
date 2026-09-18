#include "GridGame.h"
#include "GridArt.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridRainTest,"Gridbound.Combat.RainMudRecoveryAndPartialWalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridRainTest::RunTest(const FString& Parameters)
{
    const auto Circle=GridRules::RainCells(FIntPoint(10,10));
    TestEqual(TEXT("Radius four grid circle has 49 cells"),Circle.Num(),49);
    TestTrue(TEXT("Circle includes cardinal boundary"),Circle.Contains(FIntPoint(14,10)));
    TestTrue(TEXT("Circle includes (2,3), unlike a Manhattan diamond"),Circle.Contains(FIntPoint(12,13)));
    TestFalse(TEXT("Circle excludes outside diagonal"),Circle.Contains(FIntPoint(13,13)));

    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AGridPawn* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    Pawn->CurrentCell=FIntPoint(3,6); Pawn->Destination=Pawn->CurrentCell;
    Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    const FIntPoint Grass(4,6),Gravel(4,9),Healthy(5,6),Outside(0,0);
    for(const auto Cell:{Grass,Gravel}) Pawn->TerrainTiles.Add(Cell,GridArt::CreateTile(World,Cell,Pawn->BaseMaterial));
    Pawn->IgniteCell(Grass); Pawn->IgniteCell(Outside);
    TestTrue(TEXT("Rain cast valid"),Pawn->CastRain(FIntPoint(4,8)));
    TestEqual(TEXT("Fire outside rain survives"),Pawn->BurningCells.Num(),1);
    TestEqual(TEXT("Uncovered cell unchanged"),Pawn->BurningCells[0].Cell,Outside);
    TestEqual(TEXT("Extinguished grass becomes slow mud"),Pawn->MovementSpeedMultiplier(Grass,0),.5f);
    TestEqual(TEXT("Gravel becomes slow mud"),Pawn->MovementSpeedMultiplier(Gravel,0),.5f);
    TestEqual(TEXT("Healthy grass remains unchanged"),Pawn->MovementSpeedMultiplier(Healthy,0),1.f);
    TestEqual(TEXT("Character above mud is not slowed"),Pawn->MovementSpeedMultiplier(Grass,450),1.f);
    TestFalse(TEXT("Wet mud does not reignite"),Pawn->IgniteCell(Grass));
    Pawn->CurrentCell=Grass; Pawn->Destination=Grass+FIntPoint(1,0);
    Pawn->SetActorLocation(GridRules::Center(Grass,GridRules::ActorOriginHeight)); Pawn->bMoving=true; Pawn->MoveTime=0;
    Pawn->TickBurning(1.f);
    TestEqual(TEXT("Extinguished fire deals no damage"),Pawn->Health,100);
    Pawn->AdvanceMovement(.3f);
    TestTrue(TEXT("Half speed needs more than normal step time"),Pawn->bMoving);
    TestTrue(TEXT("Half speed covers half a cell in 0.3 seconds"),FMath::IsNearlyEqual(Pawn->GetActorLocation().X,GridRules::Center(Grass).X+75));
    Pawn->AdvanceMovement(.3f);
    TestFalse(TEXT("Muddy step completes in 0.6 seconds"),Pawn->bMoving);
    TestEqual(TEXT("Arrival logical cell"),Pawn->CurrentCell,Healthy);
    Pawn->Destination=Healthy+FIntPoint(1,0); Pawn->MoveTime=0; Pawn->bMoving=true;
    Pawn->AdvanceMovement(.3f);
    TestFalse(TEXT("Dry step completes in 0.3 seconds"),Pawn->bMoving);
    const int32 Count=Pawn->MuddyCells.Num();
    Pawn->TickWetTerrain(30.f); Pawn->CastRain(FIntPoint(4,8));
    TestEqual(TEXT("Repeated rain does not duplicate mud"),Pawn->MuddyCells.Num(),Count);
    Pawn->TickWetTerrain(59.9f);
    TestEqual(TEXT("Repeat rain refreshes to sixty seconds"),Pawn->MovementSpeedMultiplier(Grass,0),.5f);
    Pawn->TickWetTerrain(.11f);
    TestEqual(TEXT("Mud expires after sixty seconds"),Pawn->MuddyCells.Num(),0);
    TestEqual(TEXT("Dry ground restores full speed"),Pawn->MovementSpeedMultiplier(Grass,0),1.f);
    for(const auto Cell:{Grass,Gravel})
        TestTrue(TEXT("Original grass/gravel visual restored"),Cast<UPrimitiveComponent>(Pawn->TerrainTiles[Cell]->GetRootComponent())->IsVisible());
    TestEqual(TEXT("Gravel base type preserved"),GridArt::TerrainAt(Gravel),GridArt::ETerrain::Gravel);
    Pawn->IgniteCell(Grass); Pawn->TickBurning(10.f);
    int32 BurnIndex=INDEX_NONE;
    for(int32 I=0;I<Pawn->BurningCells.Num();++I) if(Pawn->BurningCells[I].Cell==Grass) BurnIndex=I;
    TestTrue(TEXT("Naturally extinguished grass remains charred"),BurnIndex!=INDEX_NONE);
    Pawn->TickBurning(59.9f);
    TestFalse(TEXT("Charred grass not restored too early"),Cast<UPrimitiveComponent>(Pawn->TerrainTiles[Grass]->GetRootComponent())->IsVisible());
    Pawn->TickBurning(.11f);
    TestTrue(TEXT("Charred grass restores sixty seconds after extinguishing"),Cast<UPrimitiveComponent>(Pawn->TerrainTiles[Grass]->GetRootComponent())->IsVisible());
    Pawn->CurrentCell=FIntPoint(3,6); Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    TestTrue(TEXT("Wall reaches ten cells"),Pawn->CanPlaceWall(FIntPoint(13,6)));
    TestFalse(TEXT("Wall rejects eleven cells"),Pawn->CanPlaceWall(FIntPoint(14,6)));
    AActor* Obstacle=Pawn->MakeBlock(GridRules::Center(FIntPoint(6,6),GridRules::ActorOriginHeight),FVector(1,1,1.5f),FLinearColor::Gray);
    const auto Plan=Pawn->PlaceableWallCells(FIntPoint(6,6));
    TestEqual(TEXT("One obstructed footprint cell leaves four pillars"),Plan.Num(),4);
    TestFalse(TEXT("Blocked cell omitted"),Plan.Contains(FIntPoint(6,6)));
    TestTrue(TEXT("Partial wall can cast"),Pawn->PlaceWall(FIntPoint(6,6)));
    TestEqual(TEXT("Partial preview matches generated wall"),Pawn->Walls.Num(),Plan.Num());
    TestFalse(TEXT("Fully blocked wall rejected"),Pawn->PlaceWall(FIntPoint(6,6)));
    Obstacle->Destroy();
    TestTrue(TEXT("Single gap can be filled after obstacle removal"),Pawn->PlaceWall(FIntPoint(6,6)));
    TestEqual(TEXT("Gap fill does not duplicate neighbouring columns"),Pawn->Walls.Num(),5);
    Pawn->ResetArena();
    Pawn->IgniteCell(Grass); Pawn->CastRain(Grass); Pawn->Cooldowns[2]=4.f;
    Pawn->ResetArena();
    TestEqual(TEXT("Reset clears mud"),Pawn->MuddyCells.Num(),0);
    TestEqual(TEXT("Reset clears rain visuals"),Pawn->RainEffects.Num(),0);
    TestEqual(TEXT("Reset clears rain cooldown"),Pawn->Cooldowns[2],0.f);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
