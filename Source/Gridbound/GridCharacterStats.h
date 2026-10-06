#pragma once
#include "CoreMinimal.h"

namespace GridRules
{
    enum class EAbility : uint8 { Strength,Dexterity,Constitution,Intelligence,Wisdom,Charisma };
    constexpr uint8 AbilityBit(EAbility Ability) { return uint8(1u<<uint8(Ability)); }

    struct FCharacterStats
    {
        int32 Scores[6];
        int32 Level,HitDie;
        uint8 SaveProficiencies;
        constexpr int32 Modifier(EAbility Ability) const { return Scores[uint8(Ability)]/2-5; }
        constexpr int32 Proficiency() const { return 2+(Level-1)/4; }
        constexpr int32 SaveProficiency(EAbility Ability) const { return (SaveProficiencies&AbilityBit(Ability))?Proficiency():0; }
        constexpr int32 Save(EAbility Ability) const { return Modifier(Ability)+SaveProficiency(Ability); }
        constexpr int32 Attack(EAbility Ability) const { return Modifier(Ability)+Proficiency(); }
        constexpr int32 DC(EAbility Ability) const { return 8+Attack(Ability); }
        constexpr int32 UnarmoredAC() const { return 10+Modifier(EAbility::Dexterity); }
        constexpr int32 MaxHP() const
        {
            const int32 Con=Modifier(EAbility::Constitution);
            const int32 First=HitDie+Con,Later=HitDie/2+1+Con;
            return (First>0?First:1)+(Level-1)*(Later>0?Later:1);
        }
    };

    constexpr FCharacterStats Wizard={{10,14,14,18,13,8},5,6,
        AbilityBit(EAbility::Intelligence)|AbilityBit(EAbility::Wisdom)};
    constexpr FCharacterStats Fighter={{18,13,16,8,12,10},5,10,
        AbilityBit(EAbility::Strength)|AbilityBit(EAbility::Constitution)};

    inline FString CheckSources(const FCharacterStats& Stats,EAbility Ability,int32 Roll,int32 Bonus,const TCHAR* Name,bool bAttack=false,const TCHAR* ExtraName=TEXT("修正"))
    {
        FString Text=FString::Printf(TEXT("d20(%d) %s%+d"),Roll,Name,Stats.Modifier(Ability));
        const int32 Trained=bAttack?Stats.Proficiency():Stats.SaveProficiency(Ability);
        if(Trained) Text+=FString::Printf(TEXT(" 熟练+%d"),Trained);
        const int32 Extra=Bonus-Stats.Modifier(Ability)-Trained;
        if(Extra) Text+=FString::Printf(TEXT(" %s%+d"),ExtraName,Extra);
        return Text;
    }
}
