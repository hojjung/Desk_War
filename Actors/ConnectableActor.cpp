#include "ConnectableActor.h"


#include "UObject/ConstructorHelpers.h"

// Sets default values
AConnectableActor::AConnectableActor()
{
	PrimaryActorTick.bCanEverTick = true;
	//
	m_Mesh = CreateDefaultSubobject<UPhysicsSkMeshComponent>("PhysicsSk00");
	RootComponent = m_Mesh;
	m_Mesh->SetCollisionProfileName("PhysicsActor");
	m_Mesh->SetSimulatePhysics(false);
	//
	m_PhysicsCons = CreateDefaultSubobject<UPhysicsConstraintComponent>("PhyCon00");
	m_PhysicsCons->SetupAttachment(m_Mesh);
	m_PhysicsCons->SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
	m_PhysicsCons->SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
	m_PhysicsCons->SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);
	m_PhysicsCons->SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
	m_PhysicsCons->SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
	m_PhysicsCons->SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
	m_PhysicsCons->SetDisableCollision(true);
	//
	m_MeshPort = CreateDefaultSubobject<UPortSkMeshComponent>("PortSk00");
	m_MeshPort->SetupAttachment(m_Mesh);
	m_MeshPort->CastShadow = false;
	m_MeshPort->SetGenerateOverlapEvents(true);

	m_Sphere = CreateDefaultSubobject<USphereComponent>("Sphere00");
	m_Sphere->SetupAttachment(m_Mesh);
	m_Sphere->SetSphereRadius(300.f);
	m_Sphere->SetCollisionProfileName("USBOverlap");
	//
	m_Audio = CreateDefaultSubobject<UAudioComponent>("Audio00");
	m_Audio->SetAutoActivate(false);
	static ConstructorHelpers::FObjectFinder<USoundBase> FoundSound(TEXT("SoundWave'/Game/SFX/USB/Put_in/SE_SFX_BUTTON_PUSH.SE_SFX_BUTTON_PUSH'"));
	if (FoundSound.Succeeded())
		m_Audio->SetSound(FoundSound.Object);
	else
	{
		check(FoundSound.Object);
	}
}

// Called when the game starts or when spawned
void AConnectableActor::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(false);
	m_MeshPort->InitPort(m_PhysicsCons, m_Mesh, m_Sphere, m_PortType);
	m_MeshPort->m_OnConnected.AddUObject(this, &AConnectableActor::OnConnected);
	m_MeshPort->m_OnDisconnected.AddUObject(this, &AConnectableActor::OnDisconnected);
}

void AConnectableActor::OnConnected(UPinSkMeshComponent * skComp)
{
}

void AConnectableActor::OnDisconnected(UPinSkMeshComponent * skComp)
{
}

// Called every frame
void AConnectableActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

