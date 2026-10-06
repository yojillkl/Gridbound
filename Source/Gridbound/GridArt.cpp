#include "GridArt.h"
#include "GridGame.h"
#include "ProceduralMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"


// Procedural fantasy art: smooth armour, painted terrain, cloth, foliage and elemental effects.

namespace
{
    UMaterialInterface* Surface(const TCHAR* Name,UMaterialInterface* Fallback)
    {
        const FString Path=FString::Printf(TEXT("/Game/Art/Materials/%s.%s"),Name,Name);
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path)) return Material;
        return Fallback;
    }
    // Flat normals suit stone and folded cloth; ellipsoids supply smooth normals for armour.
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
        void Cylinder(FVector Base,float BottomRadius,float TopRadius,float Height,FLinearColor Color,int32 Sides=12)
        {
            const FVector Top=Base+FVector(0,0,Height);
            for(int32 I=0;I<Sides;++I)
            {
                const float A=I*2.f*PI/Sides, B=(I+1)*2.f*PI/Sides;
                const FVector D0(FMath::Cos(A),FMath::Sin(A),0),D1(FMath::Cos(B),FMath::Sin(B),0);
                Quad(Base+D0*BottomRadius,Base+D1*BottomRadius,Top+D1*TopRadius,Top+D0*TopRadius,Color*(.96f+.04f*FMath::Cos(A)));
                Triangle(Top,Top+D0*TopRadius,Top+D1*TopRadius,Color*1.1f);
            }
        }
        void Ellipsoid(FVector Center,FVector Radius,FLinearColor Color,int32 Sides=16,int32 Rows=8)
        {
            auto Point=[&](int32 I,int32 J) { const float A=2*PI*I/Sides,B=PI*J/Rows; return Center+Radius*FVector(FMath::Sin(B)*FMath::Cos(A),FMath::Sin(B)*FMath::Sin(A),FMath::Cos(B)); };
            auto Face=[&](FVector A,FVector B,FVector C) {
                const int32 Start=Vertices.Num(); Triangle(A,B,C,Color);
                for(int32 K=Start;K<Vertices.Num();++K) Normals[K]=((Vertices[K]-Center)/(Radius*Radius)).GetSafeNormal();
            };
            for(int32 J=0;J<Rows;++J) for(int32 I=0;I<Sides;++I) {
                const FVector A=Point(I,J),B=Point(I,J+1),C=Point(I+1,J+1),D=Point(I+1,J);
                if(J<Rows-1) Face(A,B,C); if(J>0) Face(A,C,D);
            }
        }
        void Tube(FVector A,FVector B,float R0,float R1,FLinearColor Color,int32 Sides=10)
        {
            const FVector Axis=(B-A).GetSafeNormal();
            FVector U=FVector::CrossProduct(Axis,FVector::UpVector).GetSafeNormal();
            if(U.IsNearlyZero()) U=FVector::ForwardVector;
            const FVector V=FVector::CrossProduct(Axis,U);
            for(int32 I=0;I<Sides;++I) {
                const float T=2*PI*I/Sides,N=2*PI*(I+1)/Sides;
                const FVector P=U*FMath::Cos(T)+V*FMath::Sin(T),Q=U*FMath::Cos(N)+V*FMath::Sin(N);
                Quad(A+P*R0,A+Q*R0,B+Q*R1,B+P*R1,Color);
                Triangle(B,B+P*R1,B+Q*R1,Color);
            }
        }
        void Sweep(const TArray<FVector>& Path,const TArray<FVector2D>& Radii,FLinearColor Color,int32 Sides=16)
        {
            TArray<FVector> Points,Directions;
            for(int32 J=0;J<Path.Num();++J) {
                const FVector Axis=(Path[FMath::Min(J+1,Path.Num()-1)]-Path[FMath::Max(0,J-1)]).GetSafeNormal();
                const FVector U=FVector::CrossProduct(FVector::RightVector,Axis).GetSafeNormal(),V=FVector::CrossProduct(Axis,U);
                for(int32 I=0;I<Sides;++I) {
                    const float A=I*2*PI/Sides;
                    Points.Add(Path[J]+U*(FMath::Cos(A)*Radii[J].X)+V*(FMath::Sin(A)*Radii[J].Y));
                    Directions.Add((U*(FMath::Cos(A)/Radii[J].X)+V*(FMath::Sin(A)/Radii[J].Y)).GetSafeNormal());
                }
            }
            for(int32 J=0;J<Path.Num()-1;++J) for(int32 I=0;I<Sides;++I) {
                const int32 A=J*Sides+I,B=J*Sides+(I+1)%Sides,C=B+Sides,D=A+Sides,Start=Vertices.Num();
                Quad(Points[A],Points[B],Points[C],Points[D],Color);
                const int32 Order[]={A,B,C,A,C,D};
                for(int32 K=0;K<6;++K) Normals[Start+K]=Directions[Order[K]];
            }
            const int32 Top=(Path.Num()-1)*Sides;
            for(int32 I=0;I<Sides;++I) Triangle(Path.Last(),Points[Top+I],Points[Top+(I+1)%Sides],Color);
        }
        void Box(FVector C,FVector E,FLinearColor Color)
        {
            for(int32 Axis=0;Axis<3;++Axis) for(float Sign:{-1.f,1.f}) {
                FVector N=FVector::ZeroVector,U=FVector::ZeroVector,V=FVector::ZeroVector;
                N[Axis]=E[Axis]*Sign; U[(Axis+1)%3]=E[(Axis+1)%3]; V[(Axis+2)%3]=E[(Axis+2)%3]*Sign;
                Quad(C+N-U-V,C+N+U-V,C+N+U+V,C+N-U+V,Color);
            }
        }
        void Ring(FVector C,float Radius,float Width,FLinearColor Color,int32 Sides=32)
        {
            for(int32 I=0;I<Sides;++I) {
                const float A=2*PI*I/Sides,B=2*PI*(I+1)/Sides;
                const FVector P(FMath::Cos(A),FMath::Sin(A),0),Q(FMath::Cos(B),FMath::Sin(B),0);
                Quad(C+P*Radius,C+Q*Radius,C+Q*(Radius+Width),C+P*(Radius+Width),Color);
            }
        }
        UProceduralMeshComponent* Finish(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material)
        {
            // Unreal uses clockwise front faces; keep the authored outward normals on the visible side.
            for(int32 I=0;I<Indices.Num();I+=3) Swap(Indices[I+1],Indices[I+2]);
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

void GridArt::CreateScenery(UWorld* World,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Trunks,Canopy;
    FRandomStream Random(82019);
    // Forest outside the arena supplies a distant silhouette without blocking combat sightlines.
    for(int32 I=0;I<32;++I) {
        const float Along=Random.FRandRange(-800,5500),Beyond=Random.FRandRange(350,1000);
        FVector P;
        switch(I%4) {
            case 0: P=FVector(-Beyond,Along,0); break;
            case 1: P=FVector(4725+Beyond,Along,0); break;
            case 2: P=FVector(Along,-Beyond,0); break;
            default: P=FVector(Along,4725+Beyond,0); break;
        }
        const float Height=Random.FRandRange(850,1450);
        Trunks.Sweep({P,P+FVector(15,-20,Height*.33f),P+FVector(42,12,Height*.66f),P+FVector(18,0,Height)},
            {FVector2D(38,31),FVector2D(24,23),FVector2D(15,13),FVector2D(5,5)},FLinearColor(.105f,.09f,.065f),10);
        for(int32 Branch=0;Branch<10;++Branch) {
            const float Angle=Branch*137.f+I*31.f;
            const FVector Offset=FRotator(0,Angle,0).Vector()*Random.FRandRange(90,230);
            const FVector End=P+Offset+FVector(0,0,Height*(.95f-Branch*.048f));
            Trunks.Tube(P+FVector(25,0,Height*.42f),End,12,3,FLinearColor(.12f,.10f,.065f),7);
            for(int32 Lobe=0;Lobe<3;++Lobe) {
                const FVector Shift(Random.FRandRange(-75,75),Random.FRandRange(-75,75),Random.FRandRange(-45,65));
                Canopy.Ellipsoid(End+Shift,FVector(140,125,145)*Random.FRandRange(.65f,1.25f),
                    FLinearColor(.12f,.18f,.075f)*Random.FRandRange(.8f,1.15f),9,6);
            }
        }
    }
    auto* Root=Trunks.Finish(Actor,nullptr,Material); Canopy.Finish(Actor,Root,Material);
    Actor->Tags.Add(TEXT("GridScenery"));
}

AActor* GridArt::CreateMud(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    // 泥地：深色泥面 + 几个水洼 + 散落的石块，砂石底色的泥地会偏干偏亮。
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Art; FRandomStream Random(Cell.X*317+Cell.Y*911);
    const bool bGravel=TerrainAt(Cell)==ETerrain::Gravel;
    const FLinearColor Soil=bGravel?FLinearColor(.22f,.135f,.07f):FLinearColor(.125f,.07f,.035f);
    for(int32 X=0;X<4;++X) for(int32 Y=0;Y<4;++Y)
    {
        const float A=-74.5f+X*37.25f,B=-74.5f+Y*37.25f;
        Art.Triangle(FVector(A,B,2),FVector(A+37.25f,B,2),FVector(A+37.25f,B+37.25f,2),Soil*Random.FRandRange(.96f,1.04f));
        Art.Triangle(FVector(A,B,2),FVector(A+37.25f,B+37.25f,2),FVector(A,B+37.25f,2),Soil*Random.FRandRange(.96f,1.04f));
    }
    for(int32 I=0;I<4;++I)
    {
        const FVector P(Random.FRandRange(-44,44),Random.FRandRange(-44,44),2.5f);
        Art.Ellipsoid(P,FVector(Random.FRandRange(13,25),Random.FRandRange(13,25),.5f),FLinearColor(.075f,.15f,.145f),16,4);
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
    // 降雨：头顶的云层 + 三组错相下落的雨滴与地面水花，动画在 AnimateRain 里推进。
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
            Clouds.Ellipsoid(P,Radius,FLinearColor(.23f,.3f,.34f),12,8);
            Clouds.Ellipsoid(P+FVector(35,-22,-8),Radius*.72f,FLinearColor(.29f,.36f,.4f),12,8);
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
            // 四棱柱状的长雨滴从任何视角都清晰可辨。
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
    // 让三组雨滴错相循环下落，落到底部后触发水花显隐。
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
    // 纯过程化的地图布局：中央草地、河道、砂石带交错，用坐标直接判定。
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
    // 断裂的山脊：三条通道连接两侧，周围留有绕行空间。
    if(C.X==20 && ((C.Y>=5 && C.Y<=10) || (C.Y>=18 && C.Y<=21) || (C.Y>=25 && C.Y<=27))) return true;
    return (C.X==6 && (C.Y==7 || C.Y==14 || C.Y==23))
        || (C.X==16 && (C.Y==8 || C.Y==16 || C.Y==25))
        || (C.X==24 && (C.Y==11 || C.Y==22));
}

bool GridArt::PlatformAt(FIntPoint C)
{
    // 遗迹平台占据右下角一片固定区域。
    return C.X>=26 && C.X<=29 && C.Y>=14 && C.Y<=18;
}

UProceduralMeshComponent* GridArt::CreateStone(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,float Height)
{
    // Worn masonry courses keep the same gameplay footprint and top height.
    FFacets Art;
    const FVector2D Corners[]={{-67,-75},{67,-75},{75,-67},{75,67},{67,75},{-67,75},{-75,67},{-75,-67}};
    for(int32 I=0;I<8;++I)
    {
        const FVector2D A=Corners[I],B=Corners[(I+1)%8];
        const int32 Rows=FMath::Max(1,FMath::CeilToInt(Height/48.f));
        for(int32 Row=0;Row<Rows;++Row) {
            const float Z0=-Height/2+Height*Row/Rows,Z1=-Height/2+Height*(Row+1)/Rows;
            const FLinearColor Stone=FLinearColor(.255f,.285f,.25f)*(.87f+.04f*((Row*3+I)%6));
            Art.Quad(FVector(A,Z0),FVector(B,Z0),FVector(B,Z1),FVector(A,Z1),Stone*.65f);
            const float Cuts[]={0.f,Row%2?.28f:.52f,Row%2?.78f:1.f,1.f};
            for(int32 Block=0;Block<(Row%2?3:2);++Block) {
                const FVector2D P=FMath::Lerp(A,B,Cuts[Block]+.007f),Q=FMath::Lerp(A,B,Cuts[Block+1]-.007f);
                const FVector2D Out=(A+B).GetSafeNormal()*.6f;
                Art.Quad(FVector(P+Out,Z0+1),FVector(Q+Out,Z0+1),FVector(Q+Out,Z1-1),FVector(P+Out,Z1-1),Stone*(Block?.98f:1.02f));
                Art.Quad(FVector(P+Out,Z1-2),FVector(Q+Out,Z1-2),FVector(Q+Out,Z1-1),FVector(P+Out,Z1-1),Stone*1.05f);
            }
        }
        Art.Triangle(FVector(0,0,Height/2),FVector(A,Height/2),FVector(B,Height/2),FLinearColor(.28f,.33f,.22f));
    }
    FRandomStream Random(FMath::RoundToInt(Owner->GetActorLocation().X+Owner->GetActorLocation().Y*17));
    for(int32 I=0;I<8;++I) {
        const FVector P(Random.FRandRange(-62,62),Random.FRandRange(-62,62),Height*.5f+.5f);
        Art.Ellipsoid(P,FVector(11,9,1.2f),FLinearColor(.11f,.19f,.045f),8,4);
    }
    return Art.Finish(Owner,Parent,Material);
}

UProceduralMeshComponent* GridArt::CreateBeacon(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,FLinearColor Color)
{
    // 光柱标记：底座 + 立柱 + 顶部发光宝石，颜色区分晶核（金）与营地（绿）。
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
    // 火球：多切面的余烬核心 + 层层收窄的火焰尾，局部 +X 是飞行方向。
    FFacets Art;
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
    return Art.Finish(Owner,Parent,Surface(TEXT("M_FantasyMagic"),Material));
}

UProceduralMeshComponent* GridArt::CreateEarthPillar(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material,FIntPoint Cell,int32 Durability)
{
    // 土柱：倒角的方环形土层叠成，保留平整可站立顶部与一格的清晰占地；
    // 耐久越低，裂纹与剥落越明显。
    FFacets Art;
    FRandomStream Random(Cell.X*731+Cell.Y*173+51);
    const float Damage=1.f-FMath::Clamp(Durability/100.f,0.f,1.f);
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
            // 土层之间加一道深色灰缝。
            if(Layer>0) Art.Quad(A,B,B+FVector(0,0,3),A+FVector(0,0,3),Base*.53f);
        }
    }
    for(int32 Side=0;Side<8;++Side)
        Art.Triangle(FVector(0,0,225),FVector(Outline[Side].X,Outline[Side].Y,225),
            FVector(Outline[(Side+1)%8].X,Outline[(Side+1)%8].Y,225),FLinearColor(.58f,.39f,.19f));
    // 四面蔓延的黑色裂纹随拆毁伤害增长。
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
    // 燃烧草地：焦黑地表 + 成簇的火焰，火焰组件带 GridFlame 标签供动画与显隐控制。
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
    auto* Fire=Flames.Finish(Actor,Base,Surface(TEXT("M_FantasyMagic"),Material));
    Fire->ComponentTags.Add(TEXT("GridFlame"));
    Actor->SetActorLocation(GridRules::Center(Cell));
    return Actor;
}

void GridArt::AnimateBurningGrass(AActor* Actor,float Age)
{
    // 火焰随年龄上下呼吸缩放。
    TInlineComponentArray<UProceduralMeshComponent*> Meshes;
    Actor->GetComponents(Meshes);
    for(auto* Mesh:Meshes) if(Mesh->ComponentHasTag(TEXT("GridFlame")))
        Mesh->SetRelativeScale3D(FVector(1,1,.86f+.17f*FMath::Sin(Age*12.f)));
}

void GridArt::CreateRubble(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    // 碎石：土柱倒塌后的一小堆碎块，两秒后自动消失。
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
    // Continuous world-space colour removes tile seams; decoration never participates in collision.
    AActor* Actor=World->SpawnActor<AActor>();
    const ETerrain Type=TerrainAt(Cell);
    FRandomStream Random(Cell.X*9176+Cell.Y*347+1327);
    const FLinearColor GrassColor(.14f,.18f,.075f),PathColor(.255f,.235f,.18f);
    const FLinearColor Base=Type==ETerrain::Grass ? GrassColor : Type==ETerrain::River ? FLinearColor(.04f,.16f,.165f) : PathColor;
    FFacets Art;
    // 连续地表风格：保留自然色差与环境细节，不再绘制格子边框。
    constexpr int32 Subdivisions=6;
    const FVector Origin=GridRules::Center(Cell);
    auto Shade=[&](FVector P) {
        const FVector W=Origin+P;
        const float Noise=FMath::PerlinNoise2D(FVector2D(W.X,W.Y)*.003f);
        const float Detail=FMath::PerlinNoise2D(FVector2D(W.X,W.Y)*.019f);
        FLinearColor Tint=Base;
        if(Type!=ETerrain::River) {
            for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)}) {
                const ETerrain Adjacent=TerrainAt(Cell+D);
                if(Adjacent!=Type && Adjacent!=ETerrain::River) {
                    const float EdgeDistance=75.f-P.X*D.X-P.Y*D.Y;
                    const float Blend=FMath::Clamp((25.f-EdgeDistance)/50.f,0.f,.5f);
                    const FLinearColor Neighbor=Adjacent==ETerrain::Grass?GrassColor:PathColor;
                    Tint=FMath::Lerp(Tint,Neighbor,Blend);
                }
            }
        }
        // Distinctive earth patches break up the carpet without using per-tile randomness.
        if(Type==ETerrain::Grass) Tint=FMath::Lerp(Tint,PathColor*.75f,FMath::Clamp((-Noise-.06f)*1.8f,0.f,.65f));
        return Tint*(.91f+.26f*Noise+.12f*Detail);
    };
    for(int32 X=0;X<Subdivisions;++X) for(int32 Y=0;Y<Subdivisions;++Y)
    {
        const float X0=-75.f+150.f*X/Subdivisions,Y0=-75.f+150.f*Y/Subdivisions;
        const float X1=X0+150.f/Subdivisions,Y1=Y0+150.f/Subdivisions;
        const FVector A(X0,Y0,1),B(X1,Y0,1),C(X1,Y1,1),D(X0,Y1,1);
        const int32 Start=Art.Vertices.Num();
        Art.Triangle(A,B,C,Base); Art.Triangle(A,C,D,Base);
        for(int32 V=Start;V<Art.Vertices.Num();++V) Art.Colors[V]=Shade(Art.Vertices[V]);
    }
    if(Type==ETerrain::Grass)
    {
        for(int I=0;I<36;++I)
        {
            const FVector P(Random.FRandRange(-65,65),Random.FRandRange(-65,65),1.4);
            const FVector W=Origin+P;
            const float Patch=FMath::PerlinNoise2D(FVector2D(W.X+137,W.Y-223)*.007f);
            if(Patch<.04f || Random.FRand()>.68f) continue;
            for(int Blade=0;Blade<5;++Blade)
            {
                const FVector D=FRotator(0,Random.FRandRange(0,360),0).Vector();
                const float Height=Random.FRandRange(4,13)*(1.f+Patch*.6f);
                const FVector Mid=P+D*3+FVector(0,0,Height*.6f),Tip=P+D*8+FVector(0,0,Height);
                const FLinearColor Leaf=FLinearColor(.19f,.245f,.08f)*Random.FRandRange(.9f,1.12f);
                Art.Quad(P-D*.75f,P+D*.75f,Mid+D*.45f,Mid-D*.45f,Leaf*.88f);
                Art.Triangle(Mid-D*.45f,Mid+D*.45f,Tip,Leaf);
            }
            if(I<2 && Patch>.3f) {
                const FVector Bud=P+FVector(0,0,18);
                Art.Tube(P,Bud,.55f,.3f,FLinearColor(.065f,.17f,.035f),5);
                for(int32 Petal=0;Petal<5;++Petal) {
                    const FVector Offset=FRotator(0,Petal*72.f,0).Vector()*2.5f;
                    Art.Ellipsoid(Bud+Offset,FVector(2.2f,2.2f,1.3f),I%2?FLinearColor(.63f,.51f,.24f):FLinearColor(.29f,.24f,.5f),6,4);
                }
            }
        }
    }
    else if(Type==ETerrain::Gravel)
    {
        for(int I=0;I<12;++I)
        {
            const FVector P(Random.FRandRange(-68,68),Random.FRandRange(-68,68),.9f),W=Origin+P;
            const float Patch=FMath::PerlinNoise2D(FVector2D(W.X+211,W.Y+317)*.009f);
            if(Patch<.14f) continue;
            const float Size=Random.FRandRange(1.1f,4.2f);
            Art.Ellipsoid(P,FVector(Size,Size*Random.FRandRange(.5f,1.3f),Size*.3f),I%3==0?FLinearColor(.19f,.20f,.17f):FLinearColor(.28f,.26f,.21f),7,4);
        }
    }
    else
    {
        for(int I=0;I<5;++I)
        {
            const float X=Random.FRandRange(-58,58),Y=Random.FRandRange(-52,52),Length=Random.FRandRange(12,32);
            Art.Quad(FVector(X,Y,1.8),FVector(X+.6f,Y,1.8),FVector(X+3,Y+Length,1.8),FVector(X+2.4f,Y+Length,1.8),FLinearColor(.16f,.39f,.36f));
        }
        // Shallow banks and broken foam follow the real river boundary.
        for(int32 Side:{-1,1}) if(TerrainAt(Cell+FIntPoint(Side,0))!=ETerrain::River) {
            for(int32 I=0;I<14;++I) {
                const float Y=-72+I*10.5f,X=Side*(67+Random.FRandRange(-3,3));
                Art.Ellipsoid(FVector(X,Y,1),FVector(6,8,1.2f),FLinearColor(.17f,.32f,.26f),8,4);
                Art.Box(FVector(X-Side*6,Y,2),FVector(.65f,Random.FRandRange(1,3),.1f),FLinearColor(.38f,.56f,.48f));
            }
        }
    }
    auto* Mesh=Art.Finish(Actor,nullptr,Type==ETerrain::River?Surface(TEXT("M_FantasyWater"),Material):Material);
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
    const FLinearColor Cloth=bEnemy?FLinearColor(.24f,.025f,.033f):FLinearColor(.018f,.12f,.16f);
    const FLinearColor Steel(.22f,.25f,.265f),Edge(.34f,.37f,.37f),Leather(.055f,.033f,.021f),Gold(.27f,.23f,.15f);
    UMaterialInterface* Metal=Surface(TEXT("M_FantasyMetal"),Material);
    UMaterialInterface* Fabric=Surface(TEXT("M_FantasyCloth"),Material);
    FFacets Torso;
    Torso.Sweep({FVector(0,0,-24),FVector(0,0,-12),FVector(1,0,5),FVector(-1,0,16)},
        {FVector2D(12,16),FVector2D(13,17),FVector2D(13,21),FVector2D(9,16)},Steel);
    for(int32 I=0;I<3;++I) Torso.Sweep({FVector(0,0,-25+I*5),FVector(0,0,-21+I*5)},
        {FVector2D(13,17),FVector2D(12,16)},Steel*.85f);
    Torso.Sweep({FVector(0,0,-27),FVector(0,0,-23)},{FVector2D(13,17),FVector2D(13,17)},Leather);
    Torso.Box(FVector(13.2f,0,-25),FVector(.6f,3.4f,2),Gold);
    Torso.Box(FVector(14,0,-25),FVector(.2f,2.2f,1),Leather);
    // Raised breastplate ribs, rivets and the small heraldic sun.
    for(float Side:{-1.f,1.f}) {
        Torso.Tube(FVector(6,Side*17,12),FVector(12,Side*5,-12),.4f,.35f,Edge,6);
    }
    auto* Root=Torso.Finish(Owner,Parent,Metal);
    auto Part=[&](FFacets& Art,USceneComponent* Attach,FVector Position,const TCHAR* Tag)
    {
        auto* Mesh=Art.Finish(Owner,Attach,Metal);
        Mesh->SetRelativeLocation(Position); Mesh->ComponentTags.Add(FName(Tag));
        Mesh->SetOwnerNoSee(!bEnemy);
        return Mesh;
    };
    Root->SetOwnerNoSee(!bEnemy);
    FFacets Head;
    Head.Cylinder(FVector(0,0,0),8,9,8,Leather);
    Head.Ellipsoid(FVector(0,0,22),FVector(12.8f,13.4f,18),Steel,20,12);
    Head.Cylinder(FVector(0,0,5),13,12.5f,4,Edge,20);
    Head.Box(FVector(12.8f,0,23),FVector(.7f,10.8f,1.9f),FLinearColor(.012f,.017f,.019f));
    Head.Box(FVector(13.7f,0,22),FVector(.6f,1.1f,8),Gold);
    for(float Side:{-1.f,1.f}) {
        Head.Tube(FVector(13,Side*1.5f,27),FVector(9,Side*12,27),.7f,.7f,Edge,6);
        for(int32 I=0;I<3;++I) Head.Box(FVector(12.7f,Side*(3+I*2.6f),15),FVector(.35f,.65f,2.4f),Leather);
        Head.Ellipsoid(FVector(0,Side*13.3f,22),FVector(3,1.2f,3),Gold,10,6);
    }
    Head.Tube(FVector(9,0,34),FVector(-8,0,36),1.2f,1.2f,Gold);
    for(int32 I=0;I<8;++I) Head.Ellipsoid(FVector(7-I*2.5f,0,40+FMath::Sin(I*.4f)*5),FVector(3,2,5),Cloth*(1.f+I*.03f),8,6);
    Part(Head,Root,FVector(0,0,19),TEXT("Head"));
    for(int32 Side:{-1,1})
    {
        FFacets Thigh;
        Thigh.Ellipsoid(FVector(0,0,-12),FVector(7.5f,8,15),Leather);
        Thigh.Ellipsoid(FVector(4,0,-10),FVector(5.5f,8.5f,12),Steel);
        Thigh.Tube(FVector(8,Side*6,-3),FVector(8,Side*5,-17),.65f,.65f,Gold,6);
        auto* Hip=Part(Thigh,Root,FVector(0,Side*10,-26),Side<0?TEXT("LegL"):TEXT("LegR"));
        FFacets Shin;
        Shin.Cylinder(FVector(0,0,-21),5.5f,7,21,Steel,12);
        Shin.Ellipsoid(FVector(4,0,-1),FVector(5.5f,8,7),Edge);
        Shin.Tube(FVector(7,0,-7),FVector(5.5f,0,-21),.65f,.6f,Gold,6);
        Shin.Ellipsoid(FVector(4,0,-22),FVector(12,7,5),Leather);
        Shin.Ellipsoid(FVector(9,0,-21),FVector(8,7.2f,4),Steel);
        Part(Shin,Hip,FVector(0,0,-24),TEXT("Knee"));
        FFacets Arm;
        Arm.Cylinder(FVector(0,0,-20),5.5f,7,20,Leather,12);
        Arm.Ellipsoid(FVector(0,Side*1.5f,-1),FVector(10,10,4.5f),Steel);
        Arm.Ellipsoid(FVector(1,Side*3,-5),FVector(10.5f,10,2),Edge,16,6);
        Arm.Ellipsoid(FVector(3,0,-10),FVector(6,7,5),Steel);
        Arm.Ellipsoid(FVector(3,0,-20),FVector(6,7,5),Edge);
        Arm.Cylinder(FVector(0,0,-34),5.5f,6.5f,13,Steel,12);
        Arm.Cylinder(FVector(0,0,-34),5.9f,5.9f,2,Gold,12);
        Arm.Ellipsoid(FVector(1,0,-38),FVector(5,5.5f,6),Leather);
        for(int32 I=0;I<4;++I) Arm.Ellipsoid(FVector(5,-3+I*2,-37),FVector(1.4f,1,2.2f),Steel,8,4);
        auto* Shoulder=Part(Arm,Root,FVector(0,Side*25,12),Side<0?TEXT("ArmL"):TEXT("ArmR"));
        if(bEnemy && Side==1)
        {
            FFacets Sword;
            Sword.Cylinder(FVector(0,0,0),3,3,14,Leather,6);
            Sword.Cylinder(FVector(0,0,-3),5,3,4,Gold,6);
            Sword.Tube(FVector(0,-14,12),FVector(0,14,12),2,2,Gold);
            Sword.Ellipsoid(FVector(0,0,-4),FVector(4,4,4),Gold,12,6);
            for(int32 I=0;I<5;++I) Sword.Cylinder(FVector(0,0,I*2.6f),3.2f,3.2f,.6f,Edge,10);
            // 菱形截面剑刃，明确剑尖、护手与握柄。
            const FVector A(0,-5,16),B(2,0,16),C(0,5,16),D(-2,0,16),Tip(0,0,88);
            Sword.Triangle(A,B,Tip,Edge); Sword.Triangle(B,C,Tip,Steel);
            Sword.Triangle(C,D,Tip,Edge); Sword.Triangle(D,A,Tip,Steel);
            Part(Sword,Shoulder,FVector(0,0,-38),TEXT("Blade"));
        }
    }
    FFacets Cape;
    for(int32 I=0;I<10;++I) {
        const float Y0=-19+I*3.8f,Y1=Y0+3.8f;
        const float Fold0=FMath::Cos(I*PI*.6f)*2,Fold1=FMath::Cos((I+1)*PI*.6f)*2;
        const FVector A(0,Y0,0),B(0,Y1,0),C(-10+Fold1,Y1*1.17f,-52),D(-10+Fold0,Y0*1.17f,-52);
        Cape.Quad(A,D,C,B,Cloth*(.8f+.12f*FMath::Sin(I*1.3f)));
        Cape.Quad(D,C,C+FVector(0,0,1),D+FVector(0,0,1),Cloth*.65f);
    }
    auto* CapePart=Part(Cape,Root,FVector(-16,0,13),TEXT("Cape")); CapePart->SetMaterial(0,Fabric);
    FFacets Tabard;
    for(int32 I=0;I<4;++I) {
        const float Y=-9+I*4.5f;
        Tabard.Quad(FVector(17,Y,-27),FVector(18,Y+4.5f,-27),FVector(15,Y+5,-50),FVector(14,Y,-50),Cloth*(.9f+I*.05f));
    }
    Tabard.Box(FVector(16,0,-44),FVector(.7f,1,4),Gold);
    auto* Skirt=Part(Tabard,Root,FVector::ZeroVector,TEXT("Tabard")); Skirt->SetMaterial(0,Fabric);
    return Root;
}

void GridArt::AnimateAdventurer(USceneComponent* Root,float Phase,float Speed,float Windup,float Swing,float Stagger)
{
    if(!Root) return;
    const float Stride=FMath::Sin(Phase)*Speed;
    Root->SetRelativeLocation(FVector(0,0,Speed*FMath::Abs(FMath::Sin(Phase))*2.f));
    Root->SetRelativeRotation(FRotator(-Speed*3.f+Stagger*14.f,Windup*-12.f+Swing*16.f,Stagger*6.f));
    for(USceneComponent* Part:Root->GetAttachChildren())
    {
        const bool Left=Part->ComponentHasTag(TEXT("LegL"));
        if(Left || Part->ComponentHasTag(TEXT("LegR")))
        {
            const float Step=Stride*(Left?1:-1);
            Part->SetRelativeRotation(FRotator(Step*27.f,0,0));
            for(USceneComponent* Knee:Part->GetAttachChildren()) Knee->SetRelativeRotation(FRotator(-FMath::Max(0.f,Step)*32.f,0,0));
        }
        else if(Part->ComponentHasTag(TEXT("ArmL"))) Part->SetRelativeRotation(FRotator(-Stride*20.f-Windup*20.f,0,-8.f));
        else if(Part->ComponentHasTag(TEXT("ArmR"))) Part->SetRelativeRotation(FRotator(Stride*16.f-20.f-Windup*105.f+Swing*80.f,Windup*-25.f,8.f));
        else if(Part->ComponentHasTag(TEXT("Cape"))) Part->SetRelativeRotation(FRotator(Speed*12.f+FMath::Sin(Phase*.5f)*3.f,0,Stride*3.f));
        else if(Part->ComponentHasTag(TEXT("Head"))) Part->SetRelativeRotation(FRotator(Stagger*-12.f,Windup*10.f,0));
    }
}

UProceduralMeshComponent* GridArt::CreateCastingHands(AActor* Owner,USceneComponent* Parent,UMaterialInterface* Material)
{
    FFacets Empty;
    auto* Root=Empty.Finish(Owner,Parent,Material);
    Root->SetOnlyOwnerSee(true); Root->SetCastShadow(false);
    Root->SetRelativeLocation(FVector(52,0,-24));
    const FLinearColor Cloth(.06f,.095f,.10f),Leather(.105f,.065f,.041f),Stitch(.23f,.18f,.11f);
    for(float Side:{-1.f,1.f})
    {
        FFacets Hands,Sleeve;
        // A tapered palm and continuous bent fingers replace the spherical palm and straight rods.
        const FVector Wrist=FVector::ZeroVector;
        Sleeve.Sweep({FVector(0,0,0),FVector(0,0,10),FVector(0,0,20)},
            {FVector2D(4.5f,5),FVector2D(3.7f,4.2f),FVector2D(2.3f,3)},Cloth);
        Hands.Sweep({Wrist,Wrist+FVector(.3f,0,3),Wrist+FVector(.8f,0,6.5f),Wrist+FVector(1.4f,0,9)},
            {FVector2D(1.9f,2.8f),FVector2D(2.25f,3.8f),FVector2D(2.1f,4),FVector2D(1.6f,3.5f)},Leather);
        const float Lengths[]={5.2f,6.1f,5.5f,4.1f};
        for(int32 I=0;I<4;++I) {
            const float Y=(I-1.5f)*1.82f*Side,L=Lengths[I];
            const FVector A=Wrist+FVector(1.3f,Y,8.2f),B=A+FVector(.7f,(I-1.5f)*.18f,L*.48f);
            const FVector C=B+FVector(2.1f,0,L*.28f),D=C+FVector(1.9f,0,-.55f),E=D+FVector(.5f,0,-.7f);
            Hands.Sweep({A,B,C,D,E},{FVector2D(.96f,.89f),FVector2D(.92f,.84f),FVector2D(.78f,.76f),FVector2D(.67f,.68f),FVector2D(.3f,.34f)},Leather*(.97f+I*.018f));
            // Stitching and knuckle creases stay subdued rather than becoming gold jewellery.
            Hands.Tube(A+FVector(-1.3f,-.4f,0),A+FVector(-1.3f,.4f,0),.08f,.08f,Stitch,6);
        }
        Hands.Sweep({Wrist+FVector(.8f,-Side*2.6f,3),Wrist+FVector(1.2f,-Side*4.5f,5),Wrist+FVector(3.2f,-Side*5,7),Wrist+FVector(4.9f,-Side*4.4f,7.4f)},
            {FVector2D(1.4f,1.3f),FVector2D(1.1f,1),FVector2D(.93f,.88f),FVector2D(.4f,.45f)},Leather);
        // The cuff belongs to the forearm so wrist rotation cannot peel it away from the sleeve.
        Sleeve.Sweep({FVector(0,0,17),FVector(0,0,19)},
            {FVector2D(2.7f,3.4f),FVector2D(2.5f,3.2f)},Leather*.65f);
    auto* Mesh=Hands.Finish(Owner,Root,Surface(TEXT("M_FantasyCloth"),Material));
    Mesh->SetOnlyOwnerSee(true); Mesh->SetCastShadow(false);
    Mesh->ComponentTags.Add(Side<0?TEXT("GridHandL"):TEXT("GridHandR"));
    Mesh->SetRelativeLocation(FVector(Side>0?3:0,Side*25,Side>0?2:0));
    auto* Forearm=Sleeve.Finish(Owner,Root,Surface(TEXT("M_FantasyCloth"),Material));
    Forearm->ComponentTags.Add(Side<0?TEXT("GridSleeveL"):TEXT("GridSleeveR"));
    Forearm->SetOnlyOwnerSee(true); Forearm->SetCastShadow(false);
    }
    AnimateCastingHands(Root,INDEX_NONE,INDEX_NONE,0,0,0,0);
    return Root;
}

namespace
{
    void LightningRibbon(FFacets& Art,FVector A,FVector B,float Width,FLinearColor Color)
    {
        const FVector Direction=(B-A).GetSafeNormal();
        FVector Side=FVector::CrossProduct(Direction,FVector::UpVector).GetSafeNormal();
        if(Side.IsNearlyZero()) Side=FVector::RightVector;
        Side*=Width;
        Art.Quad(A-Side,A+Side,B+Side,B-Side,Color);
        const FVector Other=FVector::CrossProduct(Direction,Side).GetSafeNormal()*Width;
        Art.Quad(A-Other,A+Other,B+Other,B-Other,Color);
    }
}

AActor* GridArt::CreateLightning(UWorld* World,FVector Start,FVector End,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Art; FRandomStream Random(3917);
    const FVector Delta=End-Start;
    FVector Side=FVector::CrossProduct(Delta.GetSafeNormal(),FVector::UpVector).GetSafeNormal();
    if(Side.IsNearlyZero()) Side=FVector::RightVector;
    const int32 Segments=FMath::Max(2,FMath::CeilToInt(Delta.Size()/55.f));
    FVector Previous=FVector::ZeroVector;
    for(int32 I=1;I<=Segments;++I)
    {
        const FVector P=Delta*(float(I)/Segments)+(I==Segments?FVector::ZeroVector:Side*Random.FRandRange(-12,12));
        LightningRibbon(Art,Previous,P,3.f,FLinearColor(.1f,.55f,2.f));
        LightningRibbon(Art,Previous,P,.9f,FLinearColor(2.f,2.5f,3.f));
        Previous=P;
    }
    Art.Finish(Actor,nullptr,Surface(TEXT("M_FantasyMagic"),Material)); Actor->SetActorLocation(Start);
    return Actor;
}

AActor* GridArt::CreateElectricGround(UWorld* World,FIntPoint Cell,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    FFacets Art; FRandomStream Random(Cell.X*167+Cell.Y*941);
    for(int32 Arc=0;Arc<4;++Arc)
    {
        FVector Previous(Random.FRandRange(-62,62),Random.FRandRange(-62,62),5);
        for(int32 I=0;I<4;++I)
        {
            const FVector Next(FMath::Clamp(Previous.X+Random.FRandRange(-32,32),-68.f,68.f),
                FMath::Clamp(Previous.Y+Random.FRandRange(-32,32),-68.f,68.f),Random.FRandRange(5,16));
            LightningRibbon(Art,Previous,Next,1.5f,FLinearColor(.08f,.85f,2.2f));
            Previous=Next;
        }
    }
    Art.Finish(Actor,nullptr,Surface(TEXT("M_FantasyMagic"),Material)); Actor->SetActorLocation(GridRules::Center(Cell));
    return Actor;
}

void GridArt::AnimateElectricGround(AActor* Actor,float Age)
{
    if(!Actor) return;
    const float Pulse=.85f+.15f*FMath::Sin(Age*19.f+Actor->GetActorLocation().X);
    Actor->SetActorScale3D(FVector(Pulse,Pulse,1.f+.35f*FMath::Sin(Age*23.f)));
}

AActor* GridArt::CreateWizardEffect(UWorld* World,FVector Position,int32 Skill,UMaterialInterface* Material)
{
    AActor* Actor=World->SpawnActor<AActor>();
    if(!Actor) return nullptr;
    FFacets Art;
    if(Skill==0)
    {
        for(int32 Plane=0;Plane<3;++Plane) for(int32 I=0;I<48;++I)
        {
            const float A=2*PI*I/48.f,B=2*PI*(I+1)/48.f;
            const float Radius=GridRules::FireballBlastRadius;
            FVector P(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0),Q(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,0);
            if(Plane==1) { Swap(P.Y,P.Z); Swap(Q.Y,Q.Z); }
            if(Plane==2) { Swap(P.X,P.Z); Swap(Q.X,Q.Z); }
            Art.Tube(P,Q,5,5,FLinearColor(3,.55f,.08f),5);
        }
        Art.Ellipsoid(FVector::ZeroVector,FVector(70),FLinearColor(5,1,.1f),16,8);
    }
    else if(Skill==1)
    {
        for(int32 I=0;I<32;++I)
        {
            const float A=2*PI*I/32.f,B=2*PI*(I+1)/32.f;
            const FVector P(0,FMath::Cos(A)*58,FMath::Sin(A)*58),Q(0,FMath::Cos(B)*58,FMath::Sin(B)*58);
            Art.Tube(P,Q,1.5f,1.5f,FLinearColor(.2f,1.6f,2.6f),5);
            if(I%4==0) Art.Tube(P*.7f,P,1,1,FLinearColor(.5f,2,3),5);
        }
    }
    else if(Skill==2)
    {
        for(int32 N=1;N<=3;++N)
        {
            const float X=150.f*N;
            const FVector A(X,-225,-70),B(X,225,-70),C(X,225,150),D(X,-225,150);
            for(auto Edge:{TPair<FVector,FVector>(A,B),{B,C},{C,D},{D,A}})
                Art.Tube(Edge.Key,Edge.Value,3,3,FLinearColor(.4f,1.8f,2.6f),5);
        }
    }
    else
    {
        for(float Z:{-60.f,0.f,65.f}) Art.Ring(FVector(0,0,Z),48,3,FLinearColor(2.4f,.5f,1.4f));
        for(int32 I=0;I<4;++I)
        {
            const FVector P=FRotator(0,I*90.f,0).Vector()*48.f;
            Art.Tube(P-FVector(0,0,60),P+FVector(0,0,65),2,2,FLinearColor(1.8f,.35f,.8f),5);
        }
    }
    Art.Finish(Actor,nullptr,Surface(TEXT("M_FantasyMagic"),Material));
    Actor->SetActorLocation(Position);
    return Actor;
}
