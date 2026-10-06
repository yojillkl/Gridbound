#include "GridGame.h"
#include "GridArt.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

float AGridPawn::SkillRange(int32 Skill) const
{
    return Skill==0?GridRules::FireballRange:Skill==3?GridRules::HoldRange:
        Skill==2?GridRules::ThunderSize:0.f;
}

AGridPawn::FSpellTrace AGridPawn::TraceSpell(int32 Skill,FVector Direction) const
{
    FSpellTrace Result;
    const FVector Start=SpellOrigin();
    const FVector End=Start+Direction.GetSafeNormal()*(Skill==3?GridRules::LightningRange:SkillRange(Skill));
    Result.End=Result.ImpactPoint=End;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    // 闪电穿过角色，但绝不穿墙；预览与释放使用同一条扫掠路径。
    for(int32 Pass=0;Pass<=Targets.Num();++Pass)
    {
        FHitResult Hit;
        if(!GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(Skill==0?GridRules::FireballRadius:GridRules::LightningRadius),Params)) break;
        const int32 Enemy=Targets.IndexOfByPredicate([&](const FTarget& T){return T.Health>0 && T.Actor.Get()==Hit.GetActor();});
        if(Enemy!=INDEX_NONE)
        {
            Result.Enemies.AddUnique(Enemy);
            if(Skill==3) { Params.AddIgnoredActor(Hit.GetActor()); continue; }
        }
        Result.End=Hit.Location; Result.ImpactPoint=Hit.ImpactPoint; Result.HitActor=Hit.GetActor();
        Result.bGroundImpact=Hit.GetActor() && Hit.GetActor()->ActorHasTag(TEXT("GridTerrain")) && Hit.ImpactNormal.Z>.5f;
        break;
    }
    return Result;
}

bool AGridPawn::IsConductiveCell(FIntPoint Cell) const
{
    if(!GridRules::Inside(Cell) || SurfaceHeight(Cell)>GridRules::GroundTolerance
        || (bLevelMode && GridArt::CliffAt(Cell))) return false;
    if(GridArt::TerrainAt(Cell)==GridArt::ETerrain::River) return true;
    for(const auto& Mud:MuddyCells) if(Mud.Cell==Cell && Mud.Remaining>0.f) return true;
    return false;
}

TArray<FIntPoint> AGridPawn::ConductiveArea(FIntPoint Seed) const
{
    TArray<FIntPoint> Result;
    if(!IsConductiveCell(Seed)) return Result;
    Result.Add(Seed);
    for(int32 I=0;I<Result.Num();++I)
        for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
        {
            const FIntPoint C=Result[I]+D,Offset=C-Seed;
            if(Offset.X*Offset.X+Offset.Y*Offset.Y>GridRules::ConductRadius*GridRules::ConductRadius
                || Result.Contains(C) || !IsConductiveCell(C)) continue;
            Result.Add(C);
        }
    return Result;
}

TArray<FIntPoint> AGridPawn::LightningGroundPlan(const FSpellTrace& Trace) const
{
    TArray<FIntPoint> Result;
    auto Add=[&](FIntPoint Seed){for(const FIntPoint C:ConductiveArea(Seed)) Result.AddUnique(C);};
    if(Trace.bGroundImpact) Add(GridRules::Cell(Trace.ImpactPoint));
    for(int32 I:Trace.Enemies) if(Targets.IsValidIndex(I) && Targets[I].Actor.IsValid()
        && Targets[I].FootHeight<=GridRules::GroundTolerance) Add(GridRules::Cell(Targets[I].Actor->GetActorLocation()));
    return Result;
}

void AGridPawn::ElectrifyCells(const TArray<FIntPoint>& Cells)
{
    for(const FIntPoint C:Cells)
    {
        if(!IsConductiveCell(C)) continue;
        if(auto* Existing=ElectricCells.FindByPredicate([C](const FElectricCell& E){return E.Cell==C;}))
        { Existing->Remaining=GridRules::ElectricLifetime; continue; }
        FElectricCell E; E.Cell=C;
        E.Actor=GridArt::CreateElectricGround(GetWorld(),C,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
        ElectricCells.Add(E);
    }
}

void AGridPawn::CastLightning(FVector Direction)
{
    if(Direction.IsNearlyZero() || Health<=0 || bLevelComplete) return;
    const auto Trace=TraceSpell(3,Direction);
    const auto Ground=LightningGroundPlan(Trace);
    FLightningEffect Effect;
    Effect.Actor=GridArt::CreateLightning(GetWorld(),SpellOrigin(),Trace.End,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    LightningEffects.Add(Effect);
    int32 TotalDamage=0,WetHits=0;
    for(int32 I:Trace.Enemies)
    {
        auto& T=Targets[I];
        const bool bWet=T.WetRemaining>0.f;
        const int32 Damage=GridRules::LightningDamage*(bWet?2:1);
        T.Health=FMath::Max(0,T.Health-Damage); TotalDamage+=Damage; WetHits+=int32(bWet);
        RememberWarriorPlayer(T,GetActorLocation());
        if(T.Health<=0 && T.Actor.IsValid()) T.Actor->Destroy();
    }
    ElectrifyCells(Ground);
    Feedback=FString::Printf(TEXT("闪电：命中 %d 人 / %d 伤害，潮湿增伤 %d 人；%d 格地面带电 10 秒"),
        Trace.Enemies.Num(),TotalDamage,WetHits,Ground.Num());
}

void AGridPawn::TickElectricity(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    for(auto& T:Targets) if(T.Health>0 && T.Actor.IsValid())
    {
        const FIntPoint C=GridRules::Cell(T.Actor->GetActorLocation());
        const auto* E=ElectricCells.FindByPredicate([C](const FElectricCell& Cell){return Cell.Cell==C && Cell.Remaining>0.f;});
        if(!E || T.FootHeight>GridRules::GroundTolerance || !IsConductiveCell(C)) { T.ElectricFraction=0; continue; }
        float Active=FMath::Min(DT,E->Remaining);
        if(GridArt::TerrainAt(C)!=GridArt::ETerrain::River)
            for(const auto& Mud:MuddyCells) if(Mud.Cell==C) Active=FMath::Min(Active,Mud.Remaining);
        T.ElectricFraction+=Active*GridRules::ElectricDamagePerSecond;
        const int32 Damage=FMath::FloorToInt(T.ElectricFraction+.00001f);
        T.ElectricFraction=FMath::Max(0.f,T.ElectricFraction-Damage);
        T.Health=FMath::Max(0,T.Health-Damage);
        // 持续环境伤害只会使敌人警觉，不能泄露墙后玩家的实时位置。
        if(Damage>0) bEncounterStarted=true;
        if(T.Health<=0) T.Actor->Destroy();
    }
    for(int32 I=ElectricCells.Num()-1;I>=0;--I)
    {
        auto& E=ElectricCells[I]; E.Remaining=FMath::Max(0.f,E.Remaining-DT); E.Age+=DT;
        if(E.Remaining<=0.f || !IsConductiveCell(E.Cell))
        { if(E.Actor.IsValid()) E.Actor->Destroy(); ElectricCells.RemoveAtSwap(I); }
        else GridArt::AnimateElectricGround(E.Actor.Get(),E.Age);
    }
    for(int32 I=LightningEffects.Num()-1;I>=0;--I)
    {
        auto& E=LightningEffects[I]; E.Remaining-=DT;
        if(E.Remaining<=0.f)
        { if(E.Actor.IsValid()) E.Actor->Destroy(); LightningEffects.RemoveAtSwap(I); }
    }
}

void AGridPawn::SetupLightningShowcase()
{
#if !UE_BUILD_SHIPPING
    // 独立视觉检查入口，不影响正常游戏或战斗数值。
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset(); RespawnPlayer(FIntPoint(6,10)); SpawnWarrior(FIntPoint(9,10));
    CastRain(FIntPoint(9,10)); ElectrifyCells(ConductiveArea(FIntPoint(9,10)));
    Targets[0].StaggerRemaining=20.f;
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(-8,0,0));
    SelectedSkill=3;
    FParse::Value(FCommandLine::Get(),TEXT("GridboundPreviewSkill="),SelectedSkill);
    SelectedSkill=FMath::Clamp(SelectedSkill,0,GridRules::SkillCount-1);
#endif
}
