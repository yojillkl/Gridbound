#include "GridGame.h"

void AGridPawn::AddCombatReadout(bool bPlayer,const FString& Title,const FString& Formula,const FString& Result,int32 Value,int32 Sides,const FString& Details)
{
    ++CombatReadoutSerial;
    int32 Count=0;
    for(const auto& Entry:CombatReadouts) if(Entry.bPlayer==bPlayer) ++Count;
    if(Count>=3)
    {
        const int32 Oldest=CombatReadouts.IndexOfByPredicate([bPlayer](const FCombatReadout& Entry) { return Entry.bPlayer==bPlayer; });
        CombatReadouts.RemoveAt(Oldest);
    }
    CombatReadouts.Add({bPlayer,Title,Formula,Result,Value,Sides,0.f,Details});
}

void AGridPawn::TickCombatReadouts(float DeltaSeconds)
{
    for(auto& Entry:CombatReadouts) Entry.Age+=FMath::Max(0.f,DeltaSeconds);
    CombatReadouts.RemoveAll([](const FCombatReadout& Entry) { return Entry.Age>=3.f; });
}

void AGridHUD::DrawCombatReadouts(const AGridPawn* Pawn,float Scale,float Width,float Height)
{
    int32 Rows[2]={0,0};
    for(int32 I=Pawn->CombatReadouts.Num()-1;I>=0;--I)
    {
        const auto& Entry=Pawn->CombatReadouts[I];
        const float Alpha=FMath::Clamp((3.f-Entry.Age)/.5f,0.f,1.f);
        const bool Rolling=Entry.Sides>0 && Entry.Age<.32f;
        const float X=Entry.bPlayer?Width/Scale-340.f:20.f;
        const float Y=Height/Scale*.25f+Rows[Entry.bPlayer?1:0]++*112.f;
        const FLinearColor Color=Entry.bPlayer?FLinearColor(.25f,.95f,1.f,Alpha):FLinearColor(1.f,.28f,.3f,Alpha);
        const FLinearColor Back=Entry.bPlayer?FLinearColor(.015f,.09f,.1f,.8f*Alpha):FLinearColor(.12f,.018f,.025f,.8f*Alpha);
        DrawRect(Back,X*Scale,Y*Scale,320*Scale,102*Scale);
        DrawRect(Color,(Entry.bPlayer?X+318:X)*Scale,Y*Scale,2*Scale,102*Scale);
        auto Label=[&](const FString& Value,float TX,float TY,float MaxWidth,float FontScale)
        {
            float TW,TH;
            GetTextSize(Value,TW,TH,ChineseFont,1.f);
            const float Fit=FMath::Min(FontScale,MaxWidth/FMath::Max(1.f,TW));
            DrawText(Value,Color,TX*Scale,TY*Scale,ChineseFont,Scale*Fit);
        };
        // Animation never consumes combat RNG; only the stored result is revealed.
        const FVector2D Center(X+34,Y+41);
        const float Angle=Rolling?Entry.Age*18.f:0.f;
        FVector2D Corners[6];
        for(int32 N=0;N<6;++N)
        {
            const float A=N*PI/3.f-PI/2.f+Angle;
            const float Radius=25.f*(.8f+.2f*FMath::Clamp(Entry.Age/.15f,0.f,1.f));
            Corners[N]=Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius;
        }
        auto Edge=[&](int32 A,int32 B) { DrawLine(Corners[A].X*Scale,Corners[A].Y*Scale,Corners[B].X*Scale,Corners[B].Y*Scale,Color,Scale); };
        for(int32 N=0;N<6;++N) Edge(N,(N+1)%6);
        Edge(0,2); Edge(2,4); Edge(4,0);
        const int32 Face=Rolling?1+(FMath::FloorToInt(Entry.Age*65.f)*7)%Entry.Sides:Entry.Value;
        const FString FaceText=FString::FromInt(Face);
        float FaceW,FaceH;
        GetTextSize(FaceText,FaceW,FaceH,ChineseFont,.68f);
        DrawText(FaceText,Color,(Center.X-FaceW*.5f)*Scale,(Center.Y-FaceH*.5f)*Scale,ChineseFont,Scale*.68f);
        Label(Entry.Title,X+70,Y+8,240,.68f);
        Label(Rolling?TEXT("掷骰中…"):Entry.Formula,X+70,Y+32,240,.65f);
        if(!Rolling) Label(Entry.Result,X+70,Y+56,240,.61f);
        if(!Rolling && !Entry.Details.IsEmpty()) Label(Entry.Details,X+10,Y+80,300,.59f);
    }
}
