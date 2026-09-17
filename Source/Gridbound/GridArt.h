#pragma once
#include "CoreMinimal.h"

class UMaterialInterface;
class USceneComponent;
class UProceduralMeshComponent;
class AActor;
class UWorld;

namespace GridArt
{
    enum class ETerrain : uint8 { Grass, River, Gravel };
    ETerrain TerrainAt(FIntPoint Cell);
    bool CliffAt(FIntPoint Cell);
    bool PlatformAt(FIntPoint Cell);
    UProceduralMeshComponent* CreateStone(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, float Height);
    UProceduralMeshComponent* CreateBeacon(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, FLinearColor Color);
    AActor* CreateTile(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);
    UProceduralMeshComponent* CreateAdventurer(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, bool bEnemy);
    UProceduralMeshComponent* CreateFireball(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material);
    UProceduralMeshComponent* CreateEarthPillar(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, FIntPoint Cell, int32 Durability);
    AActor* CreateBurningGrass(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);
    void AnimateBurningGrass(AActor* Actor, float Age);
    void CreateRubble(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);
    AActor* CreateMud(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);
    AActor* CreateRain(UWorld* World, FIntPoint CenterCell, UMaterialInterface* Material);
    void AnimateRain(AActor* Actor, float Age);
}
