#pragma once
// GridArt：程序化美术层。用参数化的 procedural mesh 生成石柱、光柱、地砖、冒险者、
// 火球、土柱、燃烧草地、碎石、泥地与雨水等全部视觉资产，并与玩法逻辑解耦。
#include "CoreMinimal.h"

class UMaterialInterface;
class USceneComponent;
class UProceduralMeshComponent;
class AActor;
class UWorld;

namespace GridArt
{
    enum class ETerrain : uint8 { Grass, River, Gravel }; // 三种地表：草地、河道、砂石
    ETerrain TerrainAt(FIntPoint Cell);                  // 查询某格的地表类型
    bool CliffAt(FIntPoint Cell);                        // 该格是否是悬崖
    bool PlatformAt(FIntPoint Cell);                     // 该格是否是 300 厘米高的遗迹平台
    UProceduralMeshComponent* CreateStone(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, float Height);
    UProceduralMeshComponent* CreateBeacon(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, FLinearColor Color);
    AActor* CreateTile(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);           // 单格地表块
    UProceduralMeshComponent* CreateAdventurer(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, bool bEnemy);
    UProceduralMeshComponent* CreateFireball(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material);
    UProceduralMeshComponent* CreateEarthPillar(AActor* Owner, USceneComponent* Parent, UMaterialInterface* Material, FIntPoint Cell, int32 Durability);
    AActor* CreateBurningGrass(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);   // 燃烧草地
    void AnimateBurningGrass(AActor* Actor, float Age);                                        // 推进火焰动画
    void CreateRubble(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);            // 土柱倒塌后的碎石
    AActor* CreateMud(UWorld* World, FIntPoint Cell, UMaterialInterface* Material);            // 泥地
    AActor* CreateRain(UWorld* World, FIntPoint CenterCell, UMaterialInterface* Material);     // 降雨表现
    void AnimateRain(AActor* Actor, float Age);                                                // 推进雨水动画
}
