#include "GridGame.h"
#include "GridArt.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"

void AGridPawn::ResetWizardState()
{
    CancelSpellCast();
    EndConcentration();
    PendingAttacks.Reset();
    CombatReadouts.Reset();
    CombatReadoutSerial=0;
    for(auto& V:SpellVisuals) if(V.Actor.IsValid()) V.Actor->Destroy();
    SpellVisuals.Reset();
    if(ShieldVisual.IsValid()) ShieldVisual->Destroy();
    ShieldVisual.Reset();
    for(int32 I=0;I<3;++I) SpellSlots[I]=GridRules::MaxSpellSlots[I];
    for(int32 I=0;I<4;++I) CastLevels[I]=GridRules::SpellLevels[I];
    ActionRemaining=ShieldRemaining=ReactionRemaining=0.f;
    CombatRandom.Initialize(FMath::Rand());
}

int32 AGridPawn::RollDice(int32 Count,int32 Sides)
{
    int32 Result=0;
    for(int32 I=0;I<Count;++I) Result+=CombatRandom.RandRange(1,Sides);
    return Result;
}

bool AGridPawn::HasSpellSlot(int32 Skill) const
{
    return Skill>=0 && Skill<4 && CastLevels[Skill]>=GridRules::SpellLevels[Skill]
        && CastLevels[Skill]<=3 && SpellSlots[CastLevels[Skill]-1]>0;
}

void AGridPawn::ChangeCastLevel(int32 Delta)
{
    if(SelectedSkill<0 || SelectedSkill>=4) return;
    if(PendingCastSkill==SelectedSkill) return;
    CastLevels[SelectedSkill]=FMath::Clamp(CastLevels[SelectedSkill]+Delta,GridRules::SpellLevels[SelectedSkill],3);
}

void AGridPawn::SelectSkill(int32 Skill)
{
    if(Health<=0 || bLevelComplete || Skill<0 || Skill>=4) return;
    CancelSpellCast();
    SelectedSkill=SelectedSkill==Skill?INDEX_NONE:Skill;
}

void AGridPawn::CastFirebolt()
{
    // Retained for legacy environment tests; it is not bound to player input.
    if(Health<=0 || bLevelComplete || FireboltCooldownRemaining>0.f) return;
    const auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    LaunchFireball(PC->GetControlRotation().Vector(),true);
    FireboltCooldownRemaining=GridRules::FireboltCooldown;
}

bool AGridPawn::SpellVisible(FVector Origin,FVector End,const AActor* Target) const
{
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    for(const auto& T:Targets) if(T.Actor.IsValid()) Params.AddIgnoredActor(T.Actor.Get());
    if(Target) Params.AddIgnoredActor(Target);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Params);
}

bool AGridPawn::SpellSave(FTarget& T,bool bDexterity,FString* Details)
{
    if(bDexterity && T.HoldRemaining>0.f)
    {
        if(Details) *Details=TEXT("麻痹 · 敏捷豁免自动失败");
        return false;
    }
    const int32 Roll=RollDice(1,20),Bonus=bDexterity?T.DexteritySave:T.ConstitutionSave;
    const bool Saved=Roll+Bonus>=GridRules::SpellSaveDC;
    if(Details) *Details=GridRules::CheckSources(GridRules::Fighter,
        bDexterity?GridRules::EAbility::Dexterity:GridRules::EAbility::Constitution,Roll,Bonus,bDexterity?TEXT("敏捷"):TEXT("体质"))
        +FString::Printf(TEXT(" = %d / DC %d · %s"),Roll+Bonus,GridRules::SpellSaveDC,Saved?TEXT("半伤"):TEXT("全伤"));
    return Saved;
}

void AGridPawn::InterruptHeldTarget(FTarget& T)
{
    T.Windup=0.f; T.bCharging=false; T.ChargeRemaining=0.f; T.bWalking=false;
    T.StrikesRemaining=0; T.bSurgeFollowup=false; T.Skill=EWarriorSkill::Slash;
    T.SkillRecoveryRemaining=0.f; T.SlashRemaining=0.f; T.ThinkTime=0.f; T.TacticRemaining=0.f;
    T.TacticalShiftRemaining=0.f;
}

void AGridPawn::EndConcentration()
{
    ConcentrationRemaining=0.f;
    for(auto& T:Targets)
    {
        T.HoldRemaining=0.f; T.HoldSaveRemaining=GridRules::RoundSeconds;
        if(T.HoldVisual.IsValid()) T.HoldVisual->Destroy();
        T.HoldVisual.Reset();
    }
}

void AGridPawn::ClearSustainedSpells()
{
    CancelSpellCast();
    EndConcentration();
    PendingAttacks.Reset(); ShieldRemaining=0.f;
    if(ShieldVisual.IsValid()) ShieldVisual->Destroy();
    ShieldVisual.Reset();
    for(int32 I=Walls.Num()-1;I>=0;--I) RemoveWall(I);
    for(auto& Rain:RainEffects) if(Rain.Actor.IsValid()) Rain.Actor->Destroy();
    RainEffects.Reset(); WallEffectRemaining=0.f; RainEffectRemaining=0.f;
}

void AGridPawn::CastHoldPerson(const TArray<int32>& Enemies)
{
    EndConcentration();
    int32 Held=0;
    for(int32 I:Enemies)
    {
        auto& T=Targets[I];
        RememberWarriorPlayer(T,GetActorLocation());
        const int32 Roll=RollDice(1,20),Save=Roll+T.WisdomSave;
        AddCombatReadout(true,TEXT("人类定身术 · 目标豁免"),
            GridRules::CheckSources(GridRules::Fighter,GridRules::EAbility::Wisdom,Roll,T.WisdomSave,TEXT("感知")),
            Save>=GridRules::SpellSaveDC?TEXT("感知成功 · 抵抗定身"):TEXT("感知失败 · 麻痹 60 秒"),Save,20,
            FString::Printf(TEXT("合计 %d / DC %d = 8 + 智力%d + 熟练%d"),Save,GridRules::SpellSaveDC,GridRules::Wizard.Modifier(GridRules::EAbility::Intelligence),GridRules::Wizard.Proficiency()));
        if(Save>=GridRules::SpellSaveDC) continue;
        T.HoldRemaining=60.f; T.HoldSaveRemaining=GridRules::RoundSeconds;
        InterruptHeldTarget(T); ++Held;
        T.HoldVisual=GridArt::CreateWizardEffect(GetWorld(),T.Actor->GetActorLocation(),3,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    }
    ConcentrationRemaining=Held>0?60.f:0.f;
    Feedback=FString::Printf(TEXT("人类定身术：%d 人麻痹 / %d 人抵抗 · DC %d"),Held,Enemies.Num()-Held,GridRules::SpellSaveDC);
}

TArray<int32> AGridPawn::ThunderTargets(FVector Direction) const
{
    TArray<int32> Result;
    const FVector Forward=Direction.GetSafeNormal2D(),Side(-Forward.Y,Forward.X,0);
    for(int32 I=0;I<Targets.Num();++I)
    {
        const auto& T=Targets[I];
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        const FVector Offset=T.Actor->GetActorLocation()-GetActorLocation();
        const float X=FVector::DotProduct(Offset,Forward),Y=FVector::DotProduct(Offset,Side);
        if(X>=0 && X<=GridRules::ThunderSize && FMath::Abs(Y)<=GridRules::ThunderSize*.5f
            && T.FootHeight+150.f>FootHeight && T.FootHeight<FootHeight+GridRules::ThunderSize
            && SpellVisible(SpellOrigin(),T.Actor->GetActorLocation(),T.Actor.Get())) Result.Add(I);
    }
    return Result;
}

void AGridPawn::CastThunderwave(FVector Direction,int32 Level)
{
    const int32 Damage=RollDice(Level+1,8);
    const auto Enemies=ThunderTargets(Direction);
    int32 Total=0;
    FString SaveDetails;
    for(int32 I:Enemies)
    {
        auto& T=Targets[I];
        const bool Saved=SpellSave(T,false,SaveDetails.IsEmpty()?&SaveDetails:nullptr);
        const int32 Applied=Saved?Damage/2:Damage;
        T.Health=FMath::Max(0,T.Health-Applied); Total+=Applied;
        RememberWarriorPlayer(T,GetActorLocation());
        if(T.Health<=0) { T.Actor->Destroy(); continue; }
        if(!Saved)
        {
            const FVector Start=T.Actor->GetActorLocation();
            FVector Away=(Start-GetActorLocation()).GetSafeNormal2D();
            if(Away.IsNearlyZero()) Away=Direction.GetSafeNormal2D();
            InterruptHeldTarget(T);
            // Forced movement stops at the first obstacle and does not slide or add stun.
            for(int32 Step=0;Step<20;++Step)
            {
                const FVector Current=T.Actor->GetActorLocation(),Next=Current+Away*15.f;
                if(!WarriorCanTraverse(I,Current,Next,false)
                    || FVector::Dist2D(Next,GetActorLocation())<2.f*GridRules::WarriorRadius) break;
                bool Blocked=false;
                for(int32 J=0;J<Targets.Num();++J) if(J!=I && Targets[J].Health>0 && Targets[J].Actor.IsValid()
                    && FVector::Dist2D(Next,Targets[J].Actor->GetActorLocation())<2.f*GridRules::WarriorRadius) Blocked=true;
                if(Blocked) break;
                T.Actor->SetActorLocation(Next); T.Cell=GridRules::Cell(Next); T.MoveDestination=T.Cell;
            }
        }
    }
    auto* Effect=GridArt::CreateWizardEffect(GetWorld(),GetActorLocation(),2,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    if(Effect) Effect->SetActorRotation(FRotator(0,Direction.Rotation().Yaw,0));
    SpellVisuals.Add({Effect,.45f,.45f,2});
    Feedback=FString::Printf(TEXT("雷鸣波：%d 人 / %d 伤害 · 体质豁免 DC %d"),Enemies.Num(),Total,GridRules::SpellSaveDC);
    AddCombatReadout(true,TEXT("雷鸣波"),FString::Printf(TEXT("%dd8 = %d"),Level+1,Damage),
        FString::Printf(TEXT("命中 %d 人 · 结算 %d 伤害"),Enemies.Num(),Total),Damage,8,
        Enemies.Num()>1?TEXT("首目标：")+SaveDetails:SaveDetails);
}

void AGridPawn::ExplodeFireball(FVector Center)
{
    const int32 Damage=RollDice(8,6);
    auto* Effect=GridArt::CreateWizardEffect(GetWorld(),Center,0,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    SpellVisuals.Add({Effect,.65f,.65f,0});
    int32 Hits=0,Total=0,SelfDamage=0;
    FString SaveDetails;
    for(auto& T:Targets) if(T.Health>0 && T.Actor.IsValid()
        && FVector::Dist(Center,T.Actor->GetActorLocation())<=GridRules::FireballBlastRadius
        && SpellVisible(Center,T.Actor->GetActorLocation(),T.Actor.Get()))
    {
        const int32 Applied=SpellSave(T,true,SaveDetails.IsEmpty()?&SaveDetails:nullptr)?Damage/2:Damage;
        T.Health=FMath::Max(0,T.Health-Applied); Total+=Applied;
        RememberWarriorPlayer(T,GetActorLocation()); ++Hits;
        if(T.Health==0) T.Actor->Destroy();
    }
    // Fireball is not Sculpt Spells: the caster can be caught in the blast.
    if(Health>0 && FVector::Dist(Center,GetActorLocation())<=GridRules::FireballBlastRadius
        && SpellVisible(Center,GetActorLocation(),this))
    {
        SelfDamage=RollDice(1,20)+GridRules::Wizard.Save(GridRules::EAbility::Dexterity)>=GridRules::SpellSaveDC?Damage/2:Damage;
        TakeDamage(SelfDamage,FDamageEvent(),GetController(),this);
    }
    const FIntPoint Cell=GridRules::Cell(Center);
    for(int32 X=-4;X<=4;++X) for(int32 Y=-4;Y<=4;++Y)
    {
        const FIntPoint C=Cell+FIntPoint(X,Y);
        const FVector Ground=GridRules::Center(C,SurfaceHeight(C)+5.f);
        if(FVector::Dist(Center,Ground)<=GridRules::FireballBlastRadius && SpellVisible(Center,Ground)) IgniteCell(C);
    }
    Feedback=FString::Printf(TEXT("火球爆炸：8d6 = %d · 命中 %d 人 · 敏捷成功半伤"),Damage,Hits);
    AddCombatReadout(true,TEXT("火球术"),FString::Printf(TEXT("8d6 = %d"),Damage),
        FString::Printf(TEXT("对敌 %d 伤害 · 自伤 %d"),Total,SelfDamage),Damage,6,
        Hits>1?TEXT("首目标：")+SaveDetails:SaveDetails);
}

bool AGridPawn::QueueWarriorHit(int32 Index)
{
    if(!Targets.IsValidIndex(Index)) return false;
    auto& T=Targets[Index];
    if(T.HoldRemaining>0.f) return false;
    FPendingAttack Attack;
    Attack.Roll=RollDice(1,20); Attack.Bonus=T.AttackBonus;
    Attack.Damage=RollDice(Attack.Roll==20?2:1,8)+GridRules::Fighter.Modifier(GridRules::EAbility::Strength);
    Attack.bTrip=T.Skill==EWarriorSkill::Trip; Attack.Source=T.Actor;
    Attack.bPush=T.Skill==EWarriorSkill::Push;
    Attack.PushDirection=(GetActorLocation()-T.Actor->GetActorLocation()).GetSafeNormal2D();
    const int32 AC=GridRules::PlayerAC+(ShieldRemaining>0.f?5:0);
    if(Attack.Roll!=1 && Attack.Roll!=20 && Attack.Roll+Attack.Bonus<AC
        && Attack.Roll+Attack.Bonus+8>=AC && T.SuperiorityDice>0 && T.TripCooldown<=0.f)
    {
        --T.SuperiorityDice; Attack.Bonus+=RollDice(1,8); Attack.bPrecision=true;
        Attack.bTrip=Attack.bPush=false; T.TripCooldown=GridRules::RoundSeconds;
        Feedback=TEXT("精准攻击：卓越骰 -1");
    }
    if(Attack.Roll==1 || (Attack.Roll!=20 && Attack.Roll+Attack.Bonus<AC))
    {
        Feedback=TEXT("攻击未命中");
        AddCombatReadout(false,Attack.bPrecision?TEXT("精准攻击"):TEXT("长剑攻击"),
            GridRules::CheckSources(GridRules::Fighter,GridRules::EAbility::Strength,Attack.Roll,Attack.Bonus,TEXT("力量"),true,Attack.bPrecision?TEXT("精准"):TEXT("修正")),
            Attack.Roll==1?TEXT("自然 1 · 未命中 · 0 伤害"):TEXT("未命中 · 0 伤害"),Attack.Roll+Attack.Bonus,20,
            FString::Printf(TEXT("攻击合计 %d / AC %d"),Attack.Roll+Attack.Bonus,AC));
        return false;
    }
    if(ShieldRemaining<=0.f && ReactionRemaining<=0.f)
    {
        PendingAttacks.Add(Attack);
        Feedback=TEXT("攻击将命中 · 可使用护盾反应");
    }
    else ResolvePendingAttack(Attack);
    return true;
}

void AGridPawn::ResolvePendingAttack(const FPendingAttack& Attack)
{
    if(Health<=0 || bLevelComplete) return;
    const int32 AC=GridRules::PlayerAC+(ShieldRemaining>0.f?5:0);
    const FString Title=Attack.bPrecision?TEXT("精准攻击"):Attack.bTrip?TEXT("绊摔攻击"):Attack.bPush?TEXT("推动攻击"):TEXT("长剑攻击");
    const FString Formula=GridRules::CheckSources(GridRules::Fighter,GridRules::EAbility::Strength,Attack.Roll,Attack.Bonus,TEXT("力量"),true,Attack.bPrecision?TEXT("精准"):TEXT("修正"));
    const FString Defense=FString::Printf(TEXT("攻击合计 %d / AC %d"),Attack.Roll+Attack.Bonus,AC);
    if(Attack.Roll!=20 && Attack.Roll+Attack.Bonus<AC)
    {
        Feedback=TEXT("护盾使攻击落空");
        AddCombatReadout(false,Title,Formula,TEXT("护盾挡下 · 0 伤害"),Attack.Roll+Attack.Bonus,20,Defense);
        return;
    }
    int32 Damage=Attack.Damage;
    bool bManeuver=false;
    if(!Attack.bPrecision && (Attack.bTrip || Attack.bPush))
        for(auto& T:Targets) if(T.Actor==Attack.Source && T.SuperiorityDice>0)
        {
            --T.SuperiorityDice; T.TripCooldown=GridRules::RoundSeconds;
            T.bPreferPush=Attack.bTrip;
            Damage+=RollDice(Attack.Roll==20?2:1,8); bManeuver=true;
            break;
        }
    TakeDamage(Damage,FDamageEvent(),nullptr,Attack.Source.Get());
    AddCombatReadout(false,Title,Formula,
        FString::Printf(TEXT("%s%dd8+力量%d%s = %d 伤害"),Attack.Roll==20?TEXT("暴击 · "):TEXT(""),Attack.Roll==20?2:1,
            GridRules::Fighter.Modifier(GridRules::EAbility::Strength),bManeuver?TEXT(" + 战技骰"):TEXT(""),Damage),Attack.Roll+Attack.Bonus,20,Defense);
    if(Health>0 && bManeuver)
    {
        const int32 SaveRoll=RollDice(1,20),Save=SaveRoll+GridRules::PlayerStrengthSave;
        const bool Saved=Save>=Attack.SaveDC;
        AddCombatReadout(false,Attack.bTrip?TEXT("绊摔 · 玩家豁免"):TEXT("推动 · 玩家豁免"),
            GridRules::CheckSources(GridRules::Wizard,GridRules::EAbility::Strength,SaveRoll,GridRules::PlayerStrengthSave,TEXT("力量")),
            Saved?TEXT("力量成功 · 抵抗控制"):Attack.bTrip?TEXT("力量失败 · 倒地 0.9 秒"):TEXT("力量失败 · 击退最多 4.5 米"),Save,20,
            FString::Printf(TEXT("豁免合计 %d / 战技 DC %d"),Save,Attack.SaveDC));
        if(!Saved && Attack.bTrip)
        { TripRemaining=.9f; JumpBufferRemaining=0.f; CoyoteRemaining=0.f; }
        if(!Saved && Attack.bPush) KnockbackVelocity=Attack.PushDirection*2700.f;
        Feedback=Saved?TEXT("战技命中 · 力量豁免成功"):Attack.bTrip?TEXT("绊摔命中 · 卓越骰 -1"):TEXT("推动命中 · 卓越骰 -1");
    }
}

bool AGridPawn::ActivateShield()
{
    if(ReactionRemaining>0.f)
    { Feedback=TEXT("护盾反应尚未恢复"); return false; }
    ShieldRemaining=ReactionRemaining=GridRules::RoundSeconds;
    ShieldVisual=GridArt::CreateWizardEffect(GetWorld(),SpellOrigin(),1,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    Feedback=TEXT("自身护盾生效 · AC +5 · 持续 6 秒");
    AddCombatReadout(true,TEXT("护盾术"),TEXT("固定 AC +5"),TEXT("自身防护 · 持续 6 秒"),5,0,
        FString::Printf(TEXT("基础 AC %d + 护盾 5 = %d"),GridRules::PlayerAC,GridRules::PlayerAC+5));
    const auto Attacks=PendingAttacks; PendingAttacks.Reset();
    for(const auto& Attack:Attacks) ResolvePendingAttack(Attack);
    return true;
}

void AGridPawn::TickMageState(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    ActionRemaining=FMath::Max(0.f,ActionRemaining-DT);
    ShieldRemaining=FMath::Max(0.f,ShieldRemaining-DT);
    ReactionRemaining=FMath::Max(0.f,ReactionRemaining-DT);
    FireboltCooldownRemaining=FMath::Max(0.f,FireboltCooldownRemaining-DT);
    if(Health<=0 || bLevelComplete) { ClearSustainedSpells(); return; }
    for(int32 I=PendingAttacks.Num()-1;I>=0;--I)
    {
        PendingAttacks[I].Remaining-=DT;
        if(PendingAttacks[I].Remaining>0.f) continue;
        const auto Attack=PendingAttacks[I]; PendingAttacks.RemoveAt(I);
        ResolvePendingAttack(Attack);
        if(Health<=0) break;
    }
    bool AnyHeld=false;
    for(auto& T:Targets)
    {
        if(T.HoldRemaining>0.f)
        {
            const float Active=FMath::Min(DT,T.HoldRemaining);
            T.HoldRemaining=FMath::Max(0.f,T.HoldRemaining-DT);
            T.HoldSaveRemaining-=Active;
            while(T.HoldSaveRemaining<=0.f && T.HoldRemaining>0.f)
            {
                T.HoldSaveRemaining+=GridRules::RoundSeconds;
                const int32 Roll=RollDice(1,20),Save=Roll+T.WisdomSave;
                AddCombatReadout(false,TEXT("挣脱定身 · 感知豁免"),
                    GridRules::CheckSources(GridRules::Fighter,GridRules::EAbility::Wisdom,Roll,T.WisdomSave,TEXT("感知")),
                    Save>=GridRules::SpellSaveDC?TEXT("豁免成功 · 定身解除"):TEXT("豁免失败 · 继续定身"),Save,20,
                    FString::Printf(TEXT("豁免合计 %d / DC %d"),Save,GridRules::SpellSaveDC));
                if(Save>=GridRules::SpellSaveDC)
                { T.HoldRemaining=0.f; Feedback=TEXT("战斗大师通过感知豁免，挣脱定身"); }
            }
            if(T.Health<=0 || !T.Actor.IsValid()) T.HoldRemaining=0.f;
        }
        if(T.HoldRemaining>0.f) AnyHeld=true;
        if(T.HoldVisual.IsValid())
        {
            if(T.HoldRemaining<=0.f) { T.HoldVisual->Destroy(); T.HoldVisual.Reset(); }
            else T.HoldVisual->SetActorLocation(T.Actor->GetActorLocation());
        }
    }
    ConcentrationRemaining=AnyHeld?FMath::Max(0.f,ConcentrationRemaining-DT):0.f;
    if(ShieldVisual.IsValid())
    {
        if(ShieldRemaining<=0.f) { ShieldVisual->Destroy(); ShieldVisual.Reset(); }
        else if(const auto* PC=Cast<APlayerController>(GetController()))
        {
            ShieldVisual->SetActorLocation(SpellOrigin()+PC->GetControlRotation().Vector()*85.f);
            ShieldVisual->SetActorRotation(PC->GetControlRotation());
        }
    }
    for(int32 I=SpellVisuals.Num()-1;I>=0;--I)
    {
        auto& V=SpellVisuals[I]; V.Remaining-=DT;
        if(V.Remaining<=0.f || !V.Actor.IsValid())
        { if(V.Actor.IsValid()) V.Actor->Destroy(); SpellVisuals.RemoveAtSwap(I); continue; }
        const float Phase=1.f-V.Remaining/V.Duration;
        V.Actor->SetActorScale3D(V.Skill==0?FVector(.15f+.85f*Phase):FVector(.1f+.9f*Phase,1,1));
    }
    TickSpellCast(DT);
}
