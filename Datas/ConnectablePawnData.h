

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"

#include "Datas/USB_Enum.h"
#include "UObjects/SawPlayerBehavior.h"
#include "UObjects/IdleBehavior.h"
#include "UObjects/ConnectionBehavior.h"
#include "ConnectablePawnData.generated.h"

/**
 * 
 */
UCLASS()
class DESK_WAR_API UConnectablePawnData : public UObject
{
	GENERATED_BODY()
	
};

USTRUCT(BlueprintType)
struct FConnectablePawn_Data : public FTableRowBase
{
	GENERATED_BODY()

public:
	FConnectablePawn_Data()
	{
		m_NameID = NAME_None;
		m_bIsAI = false;
		m_IdleBehav = UIdleBehavior::StaticClass();
		m_SawPlayerBehav = USawPlayerBehavior::StaticClass();
		m_fInteractRadius=250.f;
	}

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName m_NameID;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText m_ShowingName;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EPinPortType m_PinType;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EPinPortType m_PortType;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	USkeletalMesh* m_MeshPawnMainBody;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float m_fInteractRadius;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	USkeletalMesh* m_MeshPortBody;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FVector m_PortRelativeLoc;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FRotator m_PortRelativeRot;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UConnectionBehavior> m_ConnectBehav;//�ٳ־����//����ʱⰪ�� ���? strategy
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool m_bIsAI;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<UIdleBehavior> m_IdleBehav;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TSubclassOf<USawPlayerBehavior> m_SawPlayerBehav;
};

//ai
//pawn sensing
//�����Ʈ
//fsm ����
//idle
//on player saw