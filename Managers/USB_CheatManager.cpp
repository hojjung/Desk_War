// Fill out your copyright notice in the Description page of Project Settings.


#include "USB_CheatManager.h"
#include "Datas/USB_Macros.h"
#include "Components/PhysicsMovement.h"
void UUSB_CheatManager::InitCheatManager()
{
	Super::InitCheatManager();
	PRINTF("Cheat Manager Init !");
	m_GameManager = GetWorld()->GetGameInstance<UUSB_GameManager>();
	m_PlayerController = Cast<AUSB_PlayerController>(GetWorld()->GetFirstPlayerController());
	m_Player = Cast<AUSB_PlayerPawn>( m_PlayerController->GetPawn());
}


void UUSB_CheatManager::PrintPlayerVelocity()
{
	m_Player = Cast<AUSB_PlayerPawn>(m_PlayerController->GetPawn());
	m_Player->GetMovement()->ShowVelocityAccel();
}

void UUSB_CheatManager::HidePlayerVelocity()
{
	m_Player->GetComponentByClass(UPhysicsMovement::StaticClass());
}

void UUSB_CheatManager::PrintPlayerMass()
{
	m_Player = Cast<AUSB_PlayerPawn>(m_PlayerController->GetPawn());

	float Mass= m_Player->GetTotalMass();

	PRINTF("PrintPlayerMass: %f", Mass);
}

void UUSB_CheatManager::LoadLevel(FName newLevelName)
{
	m_GameManager->LoadLevel(newLevelName);
}

void UUSB_CheatManager::UnloadLevel(FName wantLevel)
{
	m_GameManager->UnloadLevel(wantLevel);
}

