#include "ObjectGiver.h"
#include "Engine/World.h"
#include "Datas/USB_Macros.h"
// Sets default values
AObjectGiver::AObjectGiver(const FObjectInitializer& objInit):Super(objInit)
{
	PrimaryActorTick.bCanEverTick = false;
	m_nCurrentIndex = 0;
}

void AObjectGiver::BeginPlay()
{
	Super::BeginPlay();
	CreatePoolObject();
}

void AObjectGiver::CreatePoolObject()
{
	int Count = m_nPoolCount;
	FVector MyPos = GetActorLocation();
	FRotator MyRot = GetActorRotation();

	FActorSpawnParameters SpawnParam;
	SpawnParam.bNoFail = true;
	while (Count--)//순서풀이랑 비순서 풀 생각
	{
		auto* Created=GetWorld()->SpawnActor<AActor>(m_cActorWantSpawn, MyPos, MyRot, SpawnParam);
		Created->SetActorHiddenInGame(true);
		//Cast<IIPoolingObj>(Created)->OnInit(this);
		m_QPoolObj.Enqueue(Created);
	}
}

AActor* AObjectGiver::ShowActor(const FVector& pos)
{
	AActor* WantActor;

	if (m_QPoolObj.IsEmpty())
	{
		return nullptr;
	}

	if (!m_QPoolObj.Dequeue(WantActor))
	{
		//que is empty
		PRINTF("ObjIs Empty! - %s", *m_cActorWantSpawn.GetDefaultObject()->GetName());
		return nullptr;
	}

	WantActor->SetActorLocation(pos);
	WantActor->SetActorHiddenInGame(false);
	//Cast<IIPoolingObj>(WantActor)->OnPullEnque();

	return WantActor;
}

void AObjectGiver::PullBackActor(AActor * obj)
{
	m_QPoolObj.Enqueue(obj);
	//이함수만 안부르면 이소속아님
}

