// Fill out your copyright notice in the Description page of Project Settings.


#include "PinSkMeshComponent.h"
#include "GameFramework/Controller.h"
#include "Components/PortSkMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Datas/USB_Macros.h"


UPinSkMeshComponent::UPinSkMeshComponent(const FObjectInitializer& objInit) :Super(objInit)
{
	m_MyPort = nullptr;
	m_fFailImpulsePower = 10000.f;
	m_PortConnected = nullptr;
	m_NamePinBone = FName(TEXT("PinPoint"));
	m_NameVelocityPivotBone = FName(TEXT("PinPoint"));
	m_Pintype = EPinPortType::ENoneType;

	for (auto& b : m_AryTypeMatch)
	{
		b = false;
	}
}
void UPinSkMeshComponent::BeginPlay()
{
	Super::BeginPlay();
	SetPinType(m_Pintype);
}
void UPinSkMeshComponent::SetPinType(EPinPortType pinType)
{
	m_AryTypeMatch[(int)m_Pintype] = false;
	m_Pintype = pinType;
	m_AryTypeMatch[(int)m_Pintype] = true;
}


bool UPinSkMeshComponent::Connect(UPortSkMeshComponent * port)
{
	//if (!CheckTypeMatch(port->GetPortType()))
	//{
	//	return false;
	//}

	PRINTF("%s - Pin",*GetOwner()->GetName());

	m_PortConnected = port;

	
	m_OnConnectedPortBP.Broadcast(m_PortConnected);
	
	return true;
}

bool UPinSkMeshComponent::Disconnect()
{

	m_PortConnected = nullptr;
	return true;
}

bool UPinSkMeshComponent::CheckTypeMatch(EPinPortType portsType)
{
	return m_AryTypeMatch[(int)portsType];
}

void UPinSkMeshComponent::SetVelocityPivotName(FName nameWant)
{
	m_NameVelocityPivotBone = nameWant;
}

void UPinSkMeshComponent::FailConnection(const FHitResult & hitResult)
{
	AddImpulseAtLocation(((GetUpVector()*0.2f)+ GetForwardVector() * -1.f)*m_fFailImpulsePower,hitResult.ImpactPoint);


}

void UPinSkMeshComponent::SetMyPort(UPortSkMeshComponent * portMine)
{
	m_MyPort = portMine;
}




