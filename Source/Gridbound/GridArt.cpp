#include "GridArt.h"
#include "GridGame.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

namespace
{
    // Each triangle owns its vertices and normal: deliberately hard, low-poly facets.
    struct FFacets
    {
        TArray<FVector> Vertices, Normals;
        TArray<int32> Indices;
        TArray<FLinearColor> Colors;
        TArray<FVector2D> UV;
        void Triangle(FVector A,FVector B,FVector C,FLinearColor Color)
        {
            const int32 Base=Vertices.Num();
            const FVector Normal=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
            Vertices.Append({A,B,C}); Indices.Append({Base,Base+1,Base+2});
            for(const FVector P:{A,B,C}) { Normals.Add(Normal); Colors.Add(Color); UV.Add(FVector2D(P.X/150.,P.Y/150.)); }
        }
        void Quad(FVector A,FVector B,FVector C,FVector D,FLinearColor Color)
        { Triangle(A,B,C,Color); Triangle(A,C,D,Color); }
        void Rock(FVector Center,FVector Radius,FLinearColor Color,int32 Sides=5)
        {
            for(int32 I=0;I<Sides;++I)
            {
                const float A=I*2.f*PI/Sides, B=(I+1)*2.f*PI/Sides;
                const FVector P=Center+FVector(FMath::Cos(A)*Radius.X,FMath::Sin(A)*Radius.Y,0);
                const FVector Q=Center+FVector(FMath::Cos(B)*Radius.X,FMath::Sin(B)*Radius.Y,0);
                const FVector Top=Center+FVector(Radius.X*.12f,0,Radius.Z);
                Triangle(P,Q,Top,Color*(.82f+.07f*I));
            }
        }
        void Cylinder(FVector Base,float BottomRadius,float TopRadius,float Height,FLinearColor Color,int32 Sides=6)
        {
            const FVector Top=Base+FVector(0,0,Height);
            for(int32 I=0;I<Sides;++I)
            {
                const float A=I*2.f*PI/Sides, B=(I+1)*2.f*PI/Sides;
                const FVector D0(FMath::Cos(A),FMath::Sin(A),0),D1(FMath::Cos(B),FMath::Sin(B),0);
                Quad(Base+D0*BottomRadius,Base+D1*BottomRadius,Top+D1*TopRadius,Top+D0*TopRadius,Color*(.9f+.025f*I));
                Triangle(Top,Top+D0*TopRadius,Top+D1*TopRadius,Color*1.1f);
            }
        }
        UProceduralMeshComponent* Finish(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material)
        {
            auto* Mesh=NewObject<UProceduralMeshComponent>(Owner);
            Owner->AddInstanceComponent(Mesh);
            if(Parent) Mesh->SetupAttachment(Parent); else Owner->SetRootComponent(Mesh);
            Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mesh->RegisterComponent();
            TArray<FProcMeshTangent> Tangents;
            Mesh->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
            Mesh->SetMaterial(0,Material);
            return Mesh;
        }
    };
}

AActor* GridArt::CreateMud(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Art; FRandomStream Random(Cell.X*317+Cell.Y*911);
    const bool bGravel=TerrainAt(Cell)==ETerrain::Gravel;
    const FLinearColor Soil=bGravel?FLinearColor(.22f,.135f,.07f):FLinearColor(.125f,.07f,.035f);
    for(int32 X=0;X<4;++X) for(int32 Y=0;Y<4;++Y)
    {
        const float A=-74.5f+X*37.25f,B=-74.5f+Y*37.25f;
        Art.Triangle(FVector(A,B,2),FVector(A+37.25f,B,2),FVector(A+37.25f,B+37.25f,2),Soil*Random.FRandRange(.75f,1.3f));
        Art.Triangle(FVector(A,B,2),FVector(A+37.25f,B+37.25f,2),FVector(A,B+37.25f,2),Soil*Random.FRandRange(.75f,1.3f));
    }
    for(int32 I=0;I<4;++I)
    {
        const FVector P(Random.FRandRange(-44,44),Random.FRandRange(-44,44),2.5f);
        Art.Cylinder(P,Random.FRandRange(13,25),Random.FRandRange(13,25),.5f,FLinearColor(.12f,.22f,.24f),7);
        Art.Quad(P+FVector(-8,-1,1),P+FVector(6,-1,1),P+FVector(9,1,1),P+FVector(-5,1,1),FLinearColor(.36f,.47f,.43f));
    }
    for(int32 I=0;I<9;++I)
    {
        const FVector P(Random.FRandRange(-65,65),Random.FRandRange(-65,65),3);
        Art.Rock(P,FVector(4,5,bGravel?4:2),Soil*.7f);
        if(!bGravel) Art.Triangle(P-FVector(2,0,0),P+FVector(2,0,0),P+FVector(5,0,8),FLinearColor(.06f,.035f,.02f));
    }
    Art.Finish(Actor,nullptr,Material); Actor->SetActorLocation(GridRules::Center(Cell));
    return Actor;
}

AActor* GridArt::CreateRain(UWorld* World,FIntPoint CenterCell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Clouds;
    FRandomStream Random(CenterCell.X*129+CenterCell.Y*719);
    for(const FIntPoint Cell:GridRules::RainCells(CenterCell))
    {
        if(!GridRules::Inside(Cell)) continue;
        const FVector P=GridRules::Center(Cell-CenterCell,470.f+Random.FRandRange(-25,25));
        if((Cell.X+Cell.Y)%2==0)
        {
            const FVector Radius(105,105,Random.FRandRange(35,65));
            Clouds.Rock(P,Radius,FLinearColor(.31f,.41f,.5f),6);
            Clouds.Rock(P,Radius*FVector(1,1,-.5f),FLinearColor(.2f,.28f,.36f),6);
        }
    }
    auto* Root=Clouds.Finish(Actor,nullptr,Material);
    for(int32 Group=0;Group<3;++Group)
    {
        FFacets Drops,Splashes;
        for(const FIntPoint Cell:GridRules::RainCells(CenterCell))
        {
            if(!GridRules::Inside(Cell)) continue;
            const FVector P=GridRules::Center(Cell-CenterCell)+FVector(Random.FRandRange(-55,55),Random.FRandRange(-55,55),Random.FRandRange(180,420));
            // Four-sided elongated droplets remain readable from every viewing angle.
            Drops.Rock(P,FVector(2.5f,2.5f,19),FLinearColor(.32f,.75f,1.15f),4);
            Drops.Rock(P,FVector(2.5f,2.5f,-5),FLinearColor(.12f,.4f,.75f),4);
            const FVector Ground(P.X,P.Y,4);
            Splashes.Quad(Ground-FVector(6,1,0),Ground+FVector(6,-1,0),Ground+FVector(6,1,0),Ground+FVector(-6,1,0),FLinearColor(.28f,.55f,.66f));
        }
        auto* DropMesh=Drops.Finish(Actor,Root,Material); DropMesh->ComponentTags.Add(TEXT("GridRainDrops"));
        DropMesh->ComponentTags.Add(FName(*FString::FromInt(Group)));
        auto* SplashMesh=Splashes.Finish(Actor,Root,Material); SplashMesh->ComponentTags.Add(TEXT("GridRainSplash"));
        SplashMesh->ComponentTags.Add(FName(*FString::FromInt(Group)));
    }
    Actor->SetActorLocation(GridRules::Center(CenterCell));
    return Actor;
}

void GridArt::AnimateRain(AActor* Actor,float Age)
{
    TInlineComponentArray<UProceduralMeshComponent*> Meshes; Actor->GetComponents(Meshes);
    for(auto* Mesh:Meshes)
    {
        const int32 Group=Mesh->ComponentTags.Num()>1?FCString::Atoi(*Mesh->ComponentTags[1].ToString()):0;
        const float Phase=FMath::Fmod(FMath::Max(0.f,Age)*320.f+Group*83.f,280.f);
        if(Mesh->ComponentHasTag(TEXT("GridRainDrops"))) Mesh->SetRelativeLocation(FVector(0,0,100.f-Phase));
        if(Mesh->ComponentHasTag(TEXT("GridRainSplash"))) Mesh->SetVisibility(Phase>180);
    }
}

GridArt::ETerrain GridArt::TerrainAt(FIntPoint Cell)
{
    if(Cell.Y>=9 && Cell.Y<=11 && Cell.X>=9 && Cell.X<=11) return ETerrain::Grass;
    if(Cell.Y>=20 && Cell.Y<=22) return ETerrain::Gravel;
    if(Cell.X>=22 && Cell.X<=29 && Cell.Y>=14 && Cell.Y<=18) return ETerrain::Gravel;
    const int32 RiverCenter=10+(Cell.Y<5?-1:Cell.Y<13?0:1);
    if(Cell.X==RiverCenter || Cell.X==RiverCenter+1) return ETerrain::River;
    if(Cell.X==RiverCenter-1 || Cell.X==RiverCenter+2 || (Cell.Y>=9 && Cell.Y<=11)) return ETerrain::Gravel;
    return ETerrain::Grass;
}

bool GridArt::CliffAt(FIntPoint C)
{
    if(C.X==0 || C.Y==0 || C.X==GridRules::BoardSize-1 || C.Y==GridRules::BoardSize-1) return true;
    // Broken ridge: three passages connect both sides, with room to circle cover.
    if(C.X==20 && ((C.Y>=5 && C.Y<=10) || (C.Y>=18 && C.Y<=21) || (C.Y>=25 && C.Y<=27))) return true;
    return (C.X==6 && (C.Y==7 || C.Y==14 || C.Y==23))
        || (C.X==16 && (C.Y==8 || C.Y==16 || C.Y==25))
        || (C.X==24 && (C.Y==11 || C.Y==22));
}

bool GridArt::PlatformAt(FIntPoint C)
{
    return C.X>=26 && C.X<=29 && C.Y>=14 && C.Y<=18;
}

UProceduralMeshComponent* GridArt::CreateStone(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,float Height)
{
    FFacets Art;
    const FVector2D Corners[]={{-67,-75},{67,-75},{75,-67},{75,67},{67,75},{-67,75},{-75,67},{-75,-67}};
    for(int32 I=0;I<8;++I)
    {
        const FVector2D A=Corners[I],B=Corners[(I+1)%8];
        Art.Quad(FVector(A,-Height/2),FVector(B,-Height/2),FVector(B,Height/2),FVector(A,Height/2),FLinearColor(.23f,.29f,.32f)*(.8f+.06f*(I%4)));
        Art.Triangle(FVector(0,0,Height/2),FVector(A,Height/2),FVector(B,Height/2),FLinearColor(.32f,.39f,.38f));
    }
    return Art.Finish(Owner,Parent,Material);
}

UProceduralMeshComponent* GridArt::CreateBeacon(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,FLinearColor Color)
{
    FFacets Art;
    Art.Cylinder(FVector(0,0,-60),28,24,18,FLinearColor(.25f,.3f,.34f));
    Art.Cylinder(FVector(0,0,-42),12,12,55,FLinearColor(.45f,.4f,.24f));
    Art.Cylinder(FVector(0,0,13),32,27,10,FLinearColor(.28f,.32f,.35f));
    Art.Rock(FVector(0,0,50),FVector(20,20,30),Color,6);
    Art.Rock(FVector(0,0,50),FVector(20,20,-25),Color*.7f,6);
    return Art.Finish(Owner,Parent,Material);
}

UProceduralMeshComponent* GridArt::CreateFireball(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material)
{
    FFacets Art;
    // A faceted ember core and nested, tapered flame tails. Local +X is travel direction.
    for(int32 I=0;I<8;++I)
    {
        const float A=2*PI*I/8, B=2*PI*(I+1)/8;
        const FVector P(0,18*FMath::Cos(A),18*FMath::Sin(A));
        const FVector Q(0,18*FMath::Cos(B),18*FMath::Sin(B));
        Art.Triangle(FVector(22,0,0),P,Q,FLinearColor(4.f,1.9f,.12f)*(I%2?.85f:1.f));
        Art.Triangle(P,FVector(-48,0,0),Q,FLinearColor(3.f,.38f,.025f));
        const FVector Offset(0,9*FMath::Cos(A),9*FMath::Sin(A));
        Art.Triangle(P,Q,FVector(-80-I%3*9,0,0)+Offset,FLinearColor(2.5f,.14f,.012f));
    }
    for(int32 I=0;I<5;++I)
        Art.Rock(FVector(-45-I*13,(I%2?1:-1)*12,7),FVector(3,3,5),FLinearColor(4,1,.02f));
    return Art.Finish(Owner,Parent,Material);
}

UProceduralMeshComponent* GridArt::CreateEarthPillar(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,FIntPoint Cell,int32 Durability)
{
    FFacets Art;
    FRandomStream Random(Cell.X*731+Cell.Y*173+51);
    const float Damage=1.f-FMath::Clamp(Durability/100.f,0.f,1.f);
    // Chamfered square rings retain a flat walkable top and a readable one-cell footprint.
    const FVector2D Outline[]={{-62,-75},{62,-75},{75,-62},{75,62},{62,75},{-62,75},{-75,62},{-75,-62}};
    for(int32 Layer=0;Layer<6;++Layer)
    {
        const float Z0=-225+Layer*75.f, Z1=Z0+75;
        for(int32 Side=0;Side<8;++Side)
        {
            const FVector2D P=Outline[Side],Q=Outline[(Side+1)%8];
            const float Inset=Layer>0 && Layer<5?Random.FRandRange(0,5+Damage*9):0;
            const FVector A(P.X,P.Y,Z0),B(Q.X,Q.Y,Z0),C(Q.X,Q.Y,Z1),D(P.X,P.Y,Z1);
            const FVector Mid=((A+B+C+D)*.25f)*FVector(1-Inset/100.f,1-Inset/100.f,1);
            const FLinearColor Base=FLinearColor(.43f,.26f,.115f)*Random.FRandRange(.78f,1.17f)*(1-.25f*Damage);
            Art.Triangle(A,B,Mid,Base*.8f); Art.Triangle(B,C,Mid,Base);
            Art.Triangle(C,D,Mid,Base*1.13f); Art.Triangle(D,A,Mid,Base*.92f);
            // A dark mortar seam between earthen strata.
            if(Layer>0) Art.Quad(A,B,B+FVector(0,0,3),A+FVector(0,0,3),Base*.53f);
        }
    }
    for(int32 Side=0;Side<8;++Side)
        Art.Triangle(FVector(0,0,225),FVector(Outline[Side].X,Outline[Side].Y,225),
            FVector(Outline[(Side+1)%8].X,Outline[(Side+1)%8].Y,225),FLinearColor(.58f,.39f,.19f));
    // Branching black cracks on all four sides grow with demolition damage.
    if(Durability<100)
    {
        for(int32 Side=0;Side<4;++Side)
        {
            const FRotator Rotation(0,Side*90.f,0);
            const float Width=2+Damage*5;
            FVector Previous(75.5f,-18,-155);
            for(int32 Step=1;Step<=5;++Step)
            {
                const FVector Next(75.5f,(Step%2?20:-16)+Side*2,-155+Step*62.f);
                Art.Quad(Rotation.RotateVector(Previous),Rotation.RotateVector(Next),
                    Rotation.RotateVector(Next+FVector(0,Width,0)),Rotation.RotateVector(Previous+FVector(0,Width,0)),FLinearColor(.055f,.029f,.014f));
                if(Step%2) Art.Triangle(Rotation.RotateVector(Next),Rotation.RotateVector(Next+FVector(0,28*Damage,-18)),
                    Rotation.RotateVector(Next+FVector(0,Width,7)),FLinearColor(.04f,.022f,.009f));
                Previous=Next;
            }
        }
        for(int32 I=0;I<8;++I)
            Art.Rock(FVector(Random.FRandRange(-68,68),Random.FRandRange(-68,68),-223),FVector(8,10,12),FLinearColor(.3f,.17f,.065f));
    }
    if(Durability<=25)
    {
        for(int32 Side=0;Side<4;++Side)
        {
            const FRotator R(0,90.f*Side,0);
            Art.Triangle(R.RotateVector(FVector(75.8f,-35,170)),R.RotateVector(FVector(75.8f,35,145)),
                R.RotateVector(FVector(75.8f,0,80)),FLinearColor(.08f,.04f,.015f));
        }
    }
    return Art.Finish(Owner,Parent,Material);
}

AActor* GridArt::CreateBurningGrass(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Ground,Flames;
    FRandomStream Random(Cell.X*617+Cell.Y*43);
    for(int32 X=0;X<4;++X) for(int32 Y=0;Y<4;++Y)
    {
        const float A=-74.5f+X*37.25f,B=-74.5f+Y*37.25f;
        Ground.Quad(FVector(A,B,2),FVector(A+37.25f,B,2),FVector(A+37.25f,B+37.25f,2),FVector(A,B+37.25f,2),
            FLinearColor(.065f,.032f,.019f)*Random.FRandRange(.65f,1.4f));
    }
    for(int32 I=0;I<15;++I)
    {
        const FVector P(Random.FRandRange(-65,65),Random.FRandRange(-65,65),3);
        Ground.Rock(P,FVector(5,5,3),FLinearColor(.24f,.055f,.015f));
        Ground.Triangle(P-FVector(3,0,0),P+FVector(3,0,0),P+FVector(4,2,17),FLinearColor(.08f,.04f,.015f));
        const float Height=Random.FRandRange(26,62);
        Flames.Rock(P,FVector(10,10,Height),FLinearColor(3.4f,.22f,.012f));
        Flames.Rock(P+FVector(0,0,2),FVector(6,6,Height*.68f),FLinearColor(4.f,1.5f,.055f));
        Flames.Rock(P+FVector(5,2,Height+10),FVector(2,2,4),FLinearColor(4.f,1.f,.025f));
    }
    auto* Base=Ground.Finish(Actor,nullptr,Material);
    auto* Fire=Flames.Finish(Actor,Base,Material);
    Fire->ComponentTags.Add(TEXT("GridFlame"));
    Actor->SetActorLocation(GridRules::Center(Cell));
    return Actor;
}

void GridArt::AnimateBurningGrass(AActor* Actor,float Age)
{
    TInlineComponentArray<UProceduralMeshComponent*> Meshes;
    Actor->GetComponents(Meshes);
    for(auto* Mesh:Meshes) if(Mesh->ComponentHasTag(TEXT("GridFlame")))
        Mesh->SetRelativeScale3D(FVector(1,1,.86f+.17f*FMath::Sin(Age*12.f)));
}

void GridArt::CreateRubble(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Art; FRandomStream Random(Cell.X*417+Cell.Y*77);
    for(int32 I=0;I<12;++I)
        Art.Rock(FVector(Random.FRandRange(-65,65),Random.FRandRange(-65,65),1),
            FVector(Random.FRandRange(7,19),Random.FRandRange(7,19),Random.FRandRange(7,23)),FLinearColor(.34f,.2f,.08f));
    Art.Finish(Actor,nullptr,Material); Actor->SetActorLocation(GridRules::Center(Cell)); Actor->SetLifeSpan(2.f);
    Actor->Tags.Add(TEXT("GridSpellDebris"));
}

AActor* GridArt::CreateTile(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    const ETerrain Type=TerrainAt(Cell);
    FRandomStream Random(Cell.X*9176+Cell.Y*347+1327);
    const FLinearColor Base=Type==ETerrain::Grass ? FLinearColor(.16f,.42f,.07f) : Type==ETerrain::River ? FLinearColor(.025f,.39f,.48f) : FLinearColor(.62f,.44f,.22f);
    FFacets Art;
    // Subtle per-tile palette changes and thin dark seams retain readable grid units.
    const float TileShade=Random.FRandRange(.93f,1.06f);
    constexpr int32 Subdivisions=4;
    for(int32 X=0;X<Subdivisions;++X) for(int32 Y=0;Y<Subdivisions;++Y)
    {
        const float X0=-74.5f+149.f*X/Subdivisions,Y0=-74.5f+149.f*Y/Subdivisions;
        const float X1=X0+149.f/Subdivisions,Y1=Y0+149.f/Subdivisions;
        const FVector A(X0,Y0,1),B(X1,Y0,1),C(X1,Y1,1),D(X0,Y1,1);
        Art.Triangle(A,B,C,Base*TileShade*Random.FRandRange(.9f,1.1f));
        Art.Triangle(A,C,D,Base*TileShade*Random.FRandRange(.9f,1.1f));
    }
    // Beveled earthen edge is visual only; a continuous invisible floor is used for targeting.
    const FLinearColor Edge=Base*.62f;
    Art.Quad(FVector(-74.5,-74.5,1),FVector(-74.5,-74.5,-22),FVector(74.5,-74.5,-22),FVector(74.5,-74.5,1),Edge);
    Art.Quad(FVector(74.5,-74.5,1),FVector(74.5,-74.5,-22),FVector(74.5,74.5,-22),FVector(74.5,74.5,1),Edge);
    Art.Quad(FVector(74.5,74.5,1),FVector(74.5,74.5,-22),FVector(-74.5,74.5,-22),FVector(-74.5,74.5,1),Edge);
    Art.Quad(FVector(-74.5,74.5,1),FVector(-74.5,74.5,-22),FVector(-74.5,-74.5,-22),FVector(-74.5,-74.5,1),Edge);
    if(Type==ETerrain::Grass)
    {
        for(int I=0;I<11;++I)
        {
            const FVector P(Random.FRandRange(-65,65),Random.FRandRange(-65,65),1.4);
            for(int Blade=0;Blade<3;++Blade)
            {
                const FVector D=FRotator(0,Random.FRandRange(0,360),0).Vector();
                Art.Triangle(P-D*3,P+D*3,P+D*Random.FRandRange(2,8)+FVector(0,0,Random.FRandRange(9,18)),FLinearColor(.3f,.58f,.09f)*Random.FRandRange(.8f,1.2f));
            }
        }
    }
    else if(Type==ETerrain::Gravel)
    {
        for(int I=0;I<12;++I)
        {
            const float Size=Random.FRandRange(2,7);
            Art.Rock(FVector(Random.FRandRange(-63,63),Random.FRandRange(-63,63),1.1),FVector(Size,Size*.8f,Size*.6f),I%3==0?FLinearColor(.28f,.3f,.26f):FLinearColor(.82f,.68f,.42f));
        }
    }
    else
    {
        for(int I=0;I<5;++I)
        {
            const float X=Random.FRandRange(-58,58),Y=Random.FRandRange(-52,52),Length=Random.FRandRange(12,32);
            Art.Quad(FVector(X,Y,1.8),FVector(X+2,Y,1.8),FVector(X+5,Y+Length,1.8),FVector(X+3,Y+Length,1.8),FLinearColor(.3f,.78f,.8f));
        }
    }
    auto* Mesh=Art.Finish(Actor,nullptr,Material);
    auto* Floor=NewObject<UBoxComponent>(Actor);
    Actor->AddInstanceComponent(Floor); Floor->SetupAttachment(Mesh);
    Floor->SetBoxExtent(FVector(75,75,10)); Floor->SetRelativeLocation(FVector(0,0,-10));
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Floor->SetCollisionResponseToAllChannels(ECR_Block);
    Floor->RegisterComponent();
    Actor->SetActorLocation(GridRules::Center(Cell));
    Actor->Tags.Add(TEXT("GridTerrain"));
    return Actor;
}

UProceduralMeshComponent* GridArt::CreateAdventurer(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,bool bEnemy)
{
    FFacets Art;
    const FLinearColor Cloth=bEnemy ? FLinearColor(.65f,.12f,.08f):FLinearColor(.045f,.24f,.34f);
    const FLinearColor Leather(.16f,.095f,.045f),Steel(.48f,.58f,.6f),Skin(.75f,.47f,.25f);
    // Local origin is at the old pawn's center (75 cm above ground).
    for(float Y:{-11.f,11.f})
    {
        Art.Cylinder(FVector(0,Y,-72),9,8,14,Leather);
        Art.Cylinder(FVector(0,Y,-58),7,9,34,Cloth);
        Art.Cylinder(FVector(0,Y*2.3f,-16),7,8,32,Leather);
        Art.Rock(FVector(0,Y*2.2f,15),FVector(12,11,10),Steel);
    }
    Art.Cylinder(FVector(0,0,-27),20,24,47,Cloth);
    Art.Cylinder(FVector(0,0,-24),21,21,6,Leather);
    Art.Cylinder(FVector(0,0,23),12,14,22,Skin);
    Art.Cylinder(FVector(0,0,43),17,11,15,Steel);
    Art.Rock(FVector(0,0,58),FVector(11,11,7),Steel);
    // Front is +X: dark visor and brass buckle give orientation cues.
    Art.Quad(FVector(14,-10,37),FVector(14,10,37),FVector(14,10,43),FVector(14,-10,43),FLinearColor(.025f,.04f,.045f));
    Art.Quad(FVector(21,-4,-23),FVector(21,4,-23),FVector(21,4,-18),FVector(21,-4,-18),FLinearColor(.85f,.57f,.15f));
    Art.Quad(FVector(-23,-19,15),FVector(-34,-25,-50),FVector(-34,25,-50),FVector(-23,19,15),Cloth*.72f);
    return Art.Finish(Owner,Parent,Material);
}
