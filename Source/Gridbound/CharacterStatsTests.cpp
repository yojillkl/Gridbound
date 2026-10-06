#include "GridGame.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCharacterStatsTest,"Gridbound.Combat.CharacterStats",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCharacterStatsTest::RunTest(const FString& Parameters)
{
    using namespace GridRules;
    TestEqual(TEXT("Wizard HP unchanged"),Wizard.MaxHP(),32);
    TestEqual(TEXT("Fighter HP unchanged"),Fighter.MaxHP(),49);
    TestEqual(TEXT("Wizard AC unchanged"),Wizard.UnarmoredAC(),12);
    TestEqual(TEXT("Spell DC unchanged"),Wizard.DC(EAbility::Intelligence),15);
    TestEqual(TEXT("Sword attack unchanged"),Fighter.Attack(EAbility::Strength),7);
    TestEqual(TEXT("Sword damage modifier unchanged"),Fighter.Modifier(EAbility::Strength),4);
    TestEqual(TEXT("Fighter Constitution save trained"),Fighter.Save(EAbility::Constitution),6);
    TestEqual(TEXT("Fighter Wisdom save untrained"),Fighter.Save(EAbility::Wisdom),1);
    TestEqual(TEXT("Wizard Wisdom save trained"),Wizard.Save(EAbility::Wisdom),4);
    TestEqual(TEXT("Wizard Intelligence save trained"),Wizard.Save(EAbility::Intelligence),7);
    TestEqual(TEXT("Wizard Strength save untrained"),Wizard.Save(EAbility::Strength),0);
    auto Changed=Wizard;
    Changed.Scores[uint8(EAbility::Constitution)]=16;
    TestEqual(TEXT("Constitution updates every level of HP"),Changed.MaxHP(),37);
    Changed.Scores[uint8(EAbility::Intelligence)]=16;
    TestEqual(TEXT("Intelligence updates spell DC"),Changed.DC(EAbility::Intelligence),14);
    Changed.Scores[uint8(EAbility::Dexterity)]=16;
    TestEqual(TEXT("Dexterity updates AC"),Changed.UnarmoredAC(),13);
    TestEqual(TEXT("Dexterity updates save"),Changed.Save(EAbility::Dexterity),3);
    Changed.Scores[uint8(EAbility::Strength)]=9;
    TestEqual(TEXT("Odd scores below ten round down"),Changed.Modifier(EAbility::Strength),-1);
    Changed.Level=9;
    TestEqual(TEXT("Level updates proficiency"),Changed.Proficiency(),4);
    TestEqual(TEXT("Level updates trained checks"),Changed.DC(EAbility::Intelligence),15);
    const FString Attack=CheckSources(Fighter,EAbility::Strength,12,7,TEXT("力量"),true);
    TestTrue(TEXT("Attack explains ability and proficiency"),Attack.Contains(TEXT("力量+4")) && Attack.Contains(TEXT("熟练+3")));
    const FString Save=CheckSources(Fighter,EAbility::Constitution,8,6,TEXT("体质"));
    TestTrue(TEXT("Save explains ability and proficiency"),Save.Contains(TEXT("体质+3")) && Save.Contains(TEXT("熟练+3")));
    TestTrue(TEXT("Untrained saves do not claim proficiency"),!CheckSources(Fighter,EAbility::Wisdom,8,1,TEXT("感知")).Contains(TEXT("熟练")));
    TestTrue(TEXT("Precision is separate from ability"),CheckSources(Fighter,EAbility::Strength,8,12,TEXT("力量"),true,TEXT("精准")).Contains(TEXT("精准+5")));
    return !HasAnyErrors();
}
#endif
