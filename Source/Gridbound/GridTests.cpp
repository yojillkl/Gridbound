#include "GridGame.h"
#include "GridArt.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridRulesTest,"Gridbound.Rules.BoundariesAndRange",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGridRulesTest::RunTest(const FString& Parameters)
{
    for(int X=0;X<20;++X) for(int Y=0;Y<20;++Y)
    {
        const FIntPoint C(X,Y);
        TestEqual(TEXT("Cell center round trip"),GridRules::Cell(GridRules::Center(C)),C);
    }
    TestEqual(TEXT("Left side of boundary"),GridRules::Cell(FVector(74.99,0,0)),FIntPoint(0,0));
    TestEqual(TEXT("Boundary belongs to next cell"),GridRules::Cell(FVector(75,0,0)),FIntPoint(1,0));
    TestFalse(TEXT("Negative cell outside board"),GridRules::Inside(FIntPoint(-1,0)));
    TestFalse(TEXT("Upper boundary outside board"),GridRules::Inside(FIntPoint(20,19)));
    TestEqual(TEXT("Spell range retains Manhattan distance for diagonals"),GridRules::Distance(FIntPoint(0,0),FIntPoint(1,1)),2);
    TestEqual(TEXT("Range six boundary"),GridRules::Distance(FIntPoint(3,10),FIntPoint(6,13)),6);
    int Covered=0;
    for(int X=-2;X<=2;++X) for(int Y=-2;Y<=2;++Y) if(GridRules::Distance(FIntPoint(0,0),FIntPoint(X,Y))<=1) ++Covered;
    TestEqual(TEXT("Radius one covers five cells"),Covered,5);
    int32 Grass=0,River=0,Gravel=0;
    for(int X=0;X<20;++X) for(int Y=0;Y<20;++Y)
    {
        const auto Terrain=GridArt::TerrainAt(FIntPoint(X,Y));
        Grass+=Terrain==GridArt::ETerrain::Grass;
        River+=Terrain==GridArt::ETerrain::River;
        Gravel+=Terrain==GridArt::ETerrain::Gravel;
    }
    TestEqual(TEXT("All 400 cells have a terrain type"),Grass+River+Gravel,400);
    TestEqual(TEXT("Continuous two-cell-wide river"),River,40);
    TestTrue(TEXT("Grass and gravel regions both exist"),Grass>0 && Gravel>0);
    for(int Yaw=0;Yaw<360;++Yaw)
    {
        const FIntPoint Direction=GridRules::Cardinal(FRotator(0,Yaw,0).Vector());
        TestEqual(TEXT("Camera direction always selects exactly one cardinal step"),GridRules::Distance(FIntPoint::ZeroValue,Direction),1);
    }
    return true;
}
#endif
