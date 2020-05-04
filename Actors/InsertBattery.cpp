#include "InsertBattery.h"
#include "ConstructorHelpers.h"
#include "Components/PuzzleDoor.h"
#include "GameFramework/Actor.h"
#include "Actors/TwinDoor.h"

AInsertBattery::AInsertBattery()
{
	static ConstructorHelpers::FObjectFinder<USoundWave> FoundSound(TEXT("SoundWave'/Game/SFX/USB/Battery_Power_up/SE_SFX_PLAYER_POWER_UP.SE_SFX_PLAYER_POWER_UP'"));
	check(FoundSound.Object);
	m_Audio->SetSound(FoundSound.Object);
	//
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> FoundSkMesh(TEXT("SkeletalMesh'/Game/Meshes/Prop_205/Tutorial/Gate/SK_Port_Gate.SK_Port_Gate'"));
	check(FoundSkMesh.Object);
	m_Mesh->SetSkeletalMesh(FoundSkMesh.Object);
	m_Mesh->SetCollisionProfileName("BlockAll");
	//
	m_PortType = EPinPortType::E5Pin;
	//
	m_MeshPort->m_bCantMoveOnConnected = true;
	m_MeshPort->RelativeLocation = FVector(-27.000000,0.000000,-41.000000);
	//SkeletalMesh'/Game/Meshes/Prop_205/Tutorial/Gate/SK_Port_Gate.SK_Port_Gate'
	m_nMatGaugeIndex = 0;
	m_PuzzleKey = CreateDefaultSubobject<UPuzzleKey>("PuzzleKey00");
}

void AInsertBattery::BeginPlay()
{
	Super::BeginPlay();

	m_OnGivingBP.AddDynamic(this, &AInsertBattery::SetGaugeToDoor);
	m_OnFullBP.AddDynamic(this,&AInsertBattery::OnFull);
}

void AInsertBattery::SetGaugeToDoor(float gauge)
{
	auto* Door = Cast<ATwinDoor>(m_PuzzleKey->GetLinkedDoor()->GetOwner());

	if (!Door)
	{
		PRINTF("InsertBattery - ERROR - No Door Linked");
	}
	Door->SetGauge(m_nMatGaugeIndex,gauge);
}

void AInsertBattery::OnFull()
{
	m_PuzzleKey->UnlockPuzzle();
}
