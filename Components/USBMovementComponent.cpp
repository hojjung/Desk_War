


#include "USBMovementComponent.h"
#include "Components/PhysicsSkMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/Controller.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AI/Navigation/AvoidanceManager.h"
#include "Engine/Engine.h"
#include "Datas/USB_Macros.h"

const float MAX_STEP_SIDE_Z = 0.08f;
const float SWIMBOBSPEED = -80.f;
const float VERTICAL_SLOPE_NORMAL_Z = 0.001f;
const float UUSBMovementComponent::MIN_TICK_TIME = 1e-6f;
const float UUSBMovementComponent::MIN_FLOOR_DIST = 1.9f;
const float UUSBMovementComponent::MAX_FLOOR_DIST = 2.4f;
const float UUSBMovementComponent::BRAKE_TO_STOP_VELOCITY = 10.f;
const float UUSBMovementComponent::SWEEP_EDGE_REJECT_DISTANCE = 0.15f;


void FUSBFindFloorResult::SetFromSweep(const FHitResult & InHit, const float InSweepFloorDist, const bool bIsWalkableFloor)
{
	bBlockingHit = InHit.IsValidBlockingHit();
	bWalkableFloor = bIsWalkableFloor;
	bLineTrace = false;
	FloorDist = InSweepFloorDist;
	LineDist = 0.f;
	HitResult = InHit;
}

void FUSBFindFloorResult::SetFromLineTrace(const FHitResult & InHit, const float InSweepFloorDist, const float InLineDist, const bool bIsWalkableFloor)
{
	// We require a sweep that hit if we are going to use a line result.
	check(HitResult.bBlockingHit);
	if (HitResult.bBlockingHit && InHit.bBlockingHit)
	{
		// Override most of the sweep result with the line result, but save some values
		FHitResult OldHit(HitResult);
		HitResult = InHit;

		// Restore some of the old values. We want the new normals and hit actor, however.
		HitResult.Time = OldHit.Time;
		HitResult.ImpactPoint = OldHit.ImpactPoint;
		HitResult.Location = OldHit.Location;
		HitResult.TraceStart = OldHit.TraceStart;
		HitResult.TraceEnd = OldHit.TraceEnd;

		bLineTrace = true;
		FloorDist = InSweepFloorDist;
		LineDist = InLineDist;
		bWalkableFloor = bIsWalkableFloor;
	}
}


UUSBMovementComponent::UUSBMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bApplyGravityWhileJumping = true;

	GravityScale = 1.f;
	GroundFriction = 8.0f;
	JumpZVelocity = 420.0f;
	RotationRate = FRotator(0.f, 360.0f, 0.0f);
	SetWalkableFloorZ(0.71f);

	MaxWalkSpeed = 400.0f;
	m_fAirControl = 0.05f;
	AirControlBoostMultiplier = 2.f;
	AirControlBoostVelocityThreshold = 25.f;
	m_fFallingLateralFriction = 0.f;
	MaxAcceleration = 2048.0f;
	BrakingFrictionFactor = 2.0f; // Historical value, 1 would be more appropriate.
	BrakingDecelerationWalking = MaxAcceleration;
	BrakingDecelerationFalling = 0.f;
	m_fMass = 100.0f;
	m_qLastUpdateRotation = FQuat::Identity;
	m_vLastUpdateVelocity = FVector::ZeroVector;
	m_vPendingImpulseToApply = FVector::ZeroVector;
	m_vPendingLaunchVelocity = FVector::ZeroVector;
	m_eDefaultLandMovementMode = MOVE_Walking;
	m_eDefaultGroundMovementMode = MOVE_Walking;
	m_eCurrentMovementMode = m_eDefaultGroundMovementMode; // ���� �߰�

	bUseSeparateBrakingFriction = false; // Old default behavior.

	NavAgentProps.bCanJump = true;
	NavAgentProps.bCanWalk = true;
	NavAgentProps.bCanSwim = true;
	ResetMoveState();

	m_bRequestedMoveUseAcceleration = true;

	JumpKeyHoldTime = 0.0f;
	JumpMaxHoldTime = 0.0f;
	JumpMaxCount = 1;
	JumpCurrentCount = 0;
	bWasJumping = false;

	// ���� �߰��� ������
	bEnableGravity = true;
}


void UUSBMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CheckJumpInput(DeltaTime);

	const FVector InputVector = ConsumeInputVector();
	ScaleInputAcceleration(ConstrainInputAcceleration(InputVector));

	//������ 1�ƴ�?
	m_fAnalogInputModifier = ComputeAnalogInputModifier();

	if (!HasValidData() || ShouldSkipUpdate(DeltaTime)) {
		return;
	}

	PerformMovement(DeltaTime);
}

UCapsuleComponent* UUSBMovementComponent::GetBoundingCapsule() const
{
	auto* PawnSk= Cast<UPhysicsSkMeshComponent>(UpdatedComponent);

	if (!(PawnOwner && UpdatedComponent&&PawnSk))
	{
		return nullptr;
	}

	return PawnSk->GetBoundingCapsule();
}



// 543
#if WITH_EDITOR
void UUSBMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	PRINTF("PostEdit");
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const UProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UUSBMovementComponent, m_fWalkableFloorAngle))
	{
		// Compute m_fWalkableFloorZ from the Angle.
		SetWalkableFloorAngle(m_fWalkableFloorAngle);
	}
}
#endif // WITH_EDITOR

// 632
//0326 8:30
void UUSBMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (NewUpdatedComponent)
	{
		const APawn* NewPawnOwner = Cast<APawn>(NewUpdatedComponent->GetOwner());

		if (NewPawnOwner == NULL)
		{
			return;
		}

		m_MovingTarget = Cast<UPhysicsSkMeshComponent>(NewUpdatedComponent);

		if (!m_MovingTarget)
		{
			return;
		}
	}

	USceneComponent* OldUpdatedComponent = UpdatedComponent;

	Super::SetUpdatedComponent(NewUpdatedComponent);

	if (UpdatedComponent != OldUpdatedComponent)
	{
		ClearAccumulatedForces();
	}

	if (UpdatedComponent == NULL)
	{
		StopActiveMovement();
	}

	PRINTF("Update Did");
}

// 704
bool UUSBMovementComponent::HasValidData() const
{
	const bool bIsValid = UpdatedComponent && IsValid(PawnOwner);
#if ENABLE_NAN_DIAGNOSTIC
	if (bIsValid)
	{
		// NaN-checking updates
		if (Velocity.ContainsNaN())
		{
			logOrEnsureNanError(TEXT("UUSBMovementComponent::HasValidData() detected NaN/INF for (%s) in Velocity:\n%s"), *GetPathNameSafe(this), *Velocity.ToString());
			UUSBMovementComponent* MutableThis = const_cast<UUSBMovementComponent*>(this);
			MutableThis->Velocity = FVector::ZeroVector;
		}
		if (!UpdatedComponent->GetComponentTransform().IsValid())
		{
			logOrEnsureNanError(TEXT("UUSBMovementComponent::HasValidData() detected NaN/INF for (%s) in UpdatedComponent->ComponentTransform:\n%s"), *GetPathNameSafe(this), *UpdatedComponent->GetComponentTransform().ToHumanReadableString());
		}
		if (UpdatedComponent->GetComponentRotation().ContainsNaN())
		{
			logOrEnsureNanError(TEXT("UUSBMovementComponent::HasValidData() detected NaN/INF for (%s) in UpdatedComponent->GetComponentRotation():\n%s"), *GetPathNameSafe(this), *UpdatedComponent->GetComponentRotation().ToString());
		}
	}
#endif
	return bIsValid;
}

void UUSBMovementComponent::StartFalling(float timeTick, const FVector & Delta, const FVector & subLoc)
{
	// start falling
	const float DesiredDist = Delta.Size();
	const float ActualDist = (UpdatedComponent->GetComponentLocation() - subLoc).Size2D();

	if (IsMovingOnGround())
	{
		if (!GIsEditor || (GetWorld()->HasBegunPlay() && (GetWorld()->GetTimeSeconds() >= 1.f)))
		{
			SetMovementMode(MOVE_Falling); //default behavior if script didn't change physics
		}
	}

	StartNewPhysics(timeTick);
}

// 730
FCollisionShape UUSBMovementComponent::GetPawnCapsuleCollisionShape(const EShrinkCapsuleExtent ShrinkMode, const float CustomShrinkAmount) const
{
	FVector Extent = GetPawnCapsuleExtent(ShrinkMode, CustomShrinkAmount);
	return FCollisionShape::MakeCapsule(Extent);
}

// 736
FVector UUSBMovementComponent::GetPawnCapsuleExtent(const EShrinkCapsuleExtent ShrinkMode, const float CustomShrinkAmount) const
{
	check(PawnOwner);

	float Radius, HalfHeight;
	GetBoundingCapsule()->GetScaledCapsuleSize(Radius,HalfHeight);
	FVector CapsuleExtent(Radius, Radius, HalfHeight);

	float RadiusEpsilon = 0.f;
	float HeightEpsilon = 0.f;

	switch (ShrinkMode)
	{
	case SHRINK_None:
		return CapsuleExtent;

	case SHRINK_RadiusCustom:
		RadiusEpsilon = CustomShrinkAmount;
		break;

	case SHRINK_HeightCustom:
		HeightEpsilon = CustomShrinkAmount;
		break;

	case SHRINK_AllCustom:
		RadiusEpsilon = CustomShrinkAmount;
		HeightEpsilon = CustomShrinkAmount;
		break;

	default:
		//UE_LOG(LogCharacterMovement, Warning, TEXT("Unknown EShrinkCapsuleExtent in UCharacterMovementComponent::GetCapsuleExtent"));
		break;
	}

	// Don't shrink to zero extent.
	const float MinExtent = KINDA_SMALL_NUMBER * 10.f;
	CapsuleExtent.X = FMath::Max(CapsuleExtent.X - RadiusEpsilon, MinExtent);
	CapsuleExtent.Y = CapsuleExtent.X;
	CapsuleExtent.Z = FMath::Max(CapsuleExtent.Z - HeightEpsilon, MinExtent);

	return CapsuleExtent;
}

// 780
bool UUSBMovementComponent::DoJump()
{
	if (PawnOwner && CanJump())
	{
		// Don't jump if we can't move up/down.
		if (!bConstrainToPlane || FMath::Abs(PlaneConstraintNormal.Z) != 1.f)
		{
			Velocity.Z = FMath::Max(Velocity.Z, JumpZVelocity);
			SetMovementMode(MOVE_Falling);
			return true;
		}
	}

	return false;
}

// 831
void UUSBMovementComponent::Launch(FVector const& LaunchVel)
{
	if ((m_eCurrentMovementMode != MOVE_None) && IsActive() && HasValidData())
	{
		m_vPendingLaunchVelocity = LaunchVel;
	}
}

// 839
bool UUSBMovementComponent::HandlePendingLaunch()
{
	if (!m_vPendingLaunchVelocity.IsZero() && HasValidData())
	{
		Velocity = m_vPendingLaunchVelocity;
		SetMovementMode(MOVE_Falling);
		m_vPendingLaunchVelocity = FVector::ZeroVector;
		return true;
	}

	return false;
}


// 912
void UUSBMovementComponent::SetDefaultMovementMode()
{
	if (!PawnOwner || m_eCurrentMovementMode != m_eDefaultLandMovementMode)
	{
		const float SavedVelocityZ = Velocity.Z;
		SetMovementMode(m_eDefaultLandMovementMode);

		// Avoid 1-frame delay if trying to walk but walking fails at this location.
		if (m_eCurrentMovementMode == MOVE_Walking)
		{
			Velocity.Z = SavedVelocityZ; // Prevent temporary walking state from zeroing Z velocity.
			SetMovementMode(MOVE_Falling);
		}
	}
}

// 952
void UUSBMovementComponent::SetMovementMode(EMovementMode NewMovementMode)
{
	// Do nothing if nothing is changing.
	if (m_eCurrentMovementMode == NewMovementMode)
	{
		return;
	}

	const EMovementMode PrevMovementMode = m_eCurrentMovementMode;
	m_eCurrentMovementMode = NewMovementMode;

	// We allow setting movement mode before we have a component to update, in case this happens at startup.
	if (!HasValidData())
	{
		return;
	}

	// Handle change in movement mode
	OnMovementModeChanged(PrevMovementMode);

	// @todo UE4 do we need to disable ragdoll physics here? Should this function do nothing if in ragdoll?
}


void UUSBMovementComponent::AddIgnoreActorsToQuery(FCollisionQueryParams & queryParam)
{
}

// 997
void UUSBMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode)
{
	if (!HasValidData())
	{
		return;
	}
	// React to changes in the movement mode.
	if (m_eCurrentMovementMode == MOVE_Walking)
	{
		Velocity.Z = 0.f;
		m_eDefaultGroundMovementMode = m_eCurrentMovementMode;

		// make sure we update our new floor/base on initial entry of the walking physics
		FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
		//AdjustFloorHeight();
	}
	else
	{
		CurrentFloor.Clear();

		if (m_eCurrentMovementMode == MOVE_Falling)
		{
			//Velocity += GetImpartedMovementBaseVelocity();
			//CharacterOwner->Falling();	// ĳ���� �ڼտ��� ���� �������־�� �ϴ� �Լ�
		}

		if (m_eCurrentMovementMode == MOVE_None)
		{
			// Kill velocity and clear queued up events
			StopMovementKeepPathing();
			ResetJumpState();
			ClearAccumulatedForces();
		}
	}

	if (m_eCurrentMovementMode == MOVE_Falling && PreviousMovementMode != MOVE_Falling)
	{
		IPathFollowingAgentInterface* PFAgent = GetPathFollowingAgent();
		if (PFAgent)
		{
			PFAgent->OnStartedFalling();
		}
	}

	//CharacterOwner->OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// ������� Character �� OnMovementModeChanged
	if (!bPressedJump || !IsFalling())
	{
		ResetJumpState();
	}

}
void UUSBMovementComponent::PhysWalking(float deltaTime)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!PawnOwner || (!PawnOwner->Controller))
	{
		m_vAcceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	bool bCheckedFall = false;
	// Save current values
	const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FUSBFindFloorResult OldFloor = CurrentFloor;

	MaintainHorizontalGroundVelocity();//���Ʒ� �ӷ� �򸲾���
	const FVector OldVelocity = Velocity;
	m_vAcceleration.Z = 0.f;

	// Apply acceleration
	CalcVelocity(deltaTime, GroundFriction,  GetMaxBrakingDeceleration());

	// Compute move parameters
	const FVector MoveVelocity = Velocity;
	const FVector Delta = deltaTime * MoveVelocity;
	const bool bZeroDelta = Delta.IsNearlyZero();

	if (!bZeroDelta)
	{
		MoveAlongFloor(MoveVelocity, deltaTime);

		if (IsFalling())//�������� ������ �������»���
		{
			StartNewPhysics(deltaTime);
			return;
		}
	}

	//FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);

	//// Validate the floor check
	//if (CurrentFloor.IsWalkableFloor())
	//{
	//	//AdjustFloorHeight();may be need height adjust
	//}
	//else if (CurrentFloor.HitResult.bStartPenetrating)
	//{
	//	PRINTF("PhysWalk,Penetrated");
	//	// The floor check failed because it started in penetration
	//	// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
	//	FHitResult Hit(CurrentFloor.HitResult);
	//	Hit.TraceEnd = Hit.TraceStart + FVector(0.f, 0.f, MAX_FLOOR_DIST);
	//	const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
	//	ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
	//}


	//// See if we need to start falling.
	//if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
	//{
	//	/*const bool bMustJump = bZeroDelta;
	//	if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, deltaTime, bMustJump))
	//	{
	//		return;
	//	}*/

	//	bCheckedFall = true;
	//}


	// Allow overlap events and such to change physics state and velocity
	if (IsMovingOnGround())
	{
		// Make velocity reflect actual move
		if (deltaTime >= MIN_TICK_TIME)
		{
			Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / deltaTime;
		}
	}

	if (IsMovingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}
void UUSBMovementComponent::PhysFalling(float deltaTime)
{
	//if (deltaTime < MIN_TICK_TIME)
	//{
	//	return;
	//}

	//FVector FallAcceleration = GetFallingLateralAcceleration(deltaTime);
	//FallAcceleration.Z = 0.f;

	//const bool bHasAirControl = (FallAcceleration.SizeSquared2D() > 0.f);

	//const FVector OldLocation = UpdatedComponent->GetComponentLocation();
	//const FQuat PawnRotation = UpdatedComponent->GetComponentQuat();

	//FVector OldVelocity = Velocity;
	//FVector VelocityNoAirControl = Velocity;

	//	// Apply input
	//const float MaxDecel = GetMaxBrakingDeceleration();
	//// Compute VelocityNoAirControl
	//if (bHasAirControl)
	//{
	//	// Find velocity *without* acceleration.
	//	TGuardValue<FVector> RestoreAcceleration(m_vAcceleration, FVector::ZeroVector);
	//	TGuardValue<FVector> RestoreVelocity(Velocity, Velocity);
	//	Velocity.Z = 0.f;
	//	CalcVelocity(deltaTime, m_fFallingLateralFriction, false, MaxDecel);
	//	VelocityNoAirControl = FVector(Velocity.X, Velocity.Y, OldVelocity.Z);
	//}

	//TGuardValue<FVector> RestoreAcceleration(m_vAcceleration, FallAcceleration);
	//Velocity.Z = 0.f;
	//CalcVelocity(deltaTime, m_fFallingLateralFriction, false, MaxDecel);
	//Velocity.Z = OldVelocity.Z;

	//// Just copy Velocity to VelocityNoAirControl if they are the same (ie no acceleration).
	//if (!bHasAirControl)
	//{
	//	VelocityNoAirControl = Velocity;
	//}
	//// Apply gravity
	//const FVector Gravity(0.f, 0.f, GetGravityZ());
	//float GravityTime = deltaTime;
	//// If jump is providing force, gravity may be affected.
	//if (JumpForceTimeRemaining > 0.0f)
	//{
	//	// Consume some of the force time. Only the remaining time (if any) is affected by gravity when bApplyGravityWhileJumping=false.
	//	const float JumpForceTime = FMath::Min(JumpForceTimeRemaining, deltaTime);
	//	GravityTime = bApplyGravityWhileJumping ? deltaTime : FMath::Max(0.0f, deltaTime - JumpForceTime);

	//	// Update Character state
	//	JumpForceTimeRemaining -= JumpForceTime;
	//	if (JumpForceTimeRemaining <= 0.0f)
	//	{
	//		ResetJumpState();
	//	}
	//}

	//Velocity = NewFallVelocity(Velocity, Gravity, GravityTime);
	//VelocityNoAirControl = bHasAirControl ? NewFallVelocity(VelocityNoAirControl, Gravity, GravityTime) : Velocity;
	//const FVector AirControlAccel = (Velocity - VelocityNoAirControl) / deltaTime;
	//// Move
	//FHitResult Hit(1.f);
	//FVector Adjusted = 0.5f * (OldVelocity + Velocity) * deltaTime;
	////SafeMoveUpdatedComponent(Adjusted, PawnRotation, true, Hit);
	//SetPhysicalVelocity(Adjusted,&Hit);

	//float LastMoveTimeSlice = deltaTime;
	//float subTimeTickRemaining = deltaTime * (1.f - Hit.Time);

	//if (Hit.bBlockingHit)
	//{
	//	if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
	//	{
	//		remainingTime += subTimeTickRemaining;
	//		ProcessLanded(Hit, remainingTime, Iterations);
	//		return;
	//	}
	//	else
	//	{
	//		// Compute impact deflection based on final velocity, not integration step.
	//		// This allows us to compute a new velocity from the deflected vector, and ensures the full gravity effect is included in the slide result.
	//		Adjusted = Velocity * timeTick;

	//		// See if we can convert a normally invalid landing spot (based on the hit result) to a usable one.
	//		if (!Hit.bStartPenetrating && ShouldCheckForValidLandingSpot(timeTick, Adjusted, Hit))
	//		{
	//			const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	//			FFindFloorResult FloorResult;
	//			FindFloor(PawnLocation, FloorResult, false);
	//			if (FloorResult.IsWalkableFloor() && IsValidLandingSpot(PawnLocation, FloorResult.HitResult))
	//			{
	//				remainingTime += subTimeTickRemaining;
	//				ProcessLanded(FloorResult.HitResult, remainingTime, Iterations);
	//				return;
	//			}
	//		}

	//		HandleImpact(Hit, LastMoveTimeSlice, Adjusted);

	//		// If we've changed physics mode, abort.
	//		if (!HasValidData() || !IsFalling())
	//		{
	//			return;
	//		}

	//		// Limit air control based on what we hit.
	//		// We moved to the impact point using air control, but may want to deflect from there based on a limited air control acceleration.
	//		if (bHasAirControl)
	//		{
	//			const bool bCheckLandingSpot = false; // we already checked above.
	//			const FVector AirControlDeltaV = LimitAirControl(LastMoveTimeSlice, AirControlAccel, Hit, bCheckLandingSpot) * LastMoveTimeSlice;
	//			Adjusted = (VelocityNoAirControl + AirControlDeltaV) * LastMoveTimeSlice;
	//		}

	//		const FVector OldHitNormal = Hit.Normal;
	//		const FVector OldHitImpactNormal = Hit.ImpactNormal;
	//		FVector Delta = ComputeSlideVector(Adjusted, 1.f - Hit.Time, OldHitNormal, Hit);

	//		// Compute velocity after deflection (only gravity component for RootMotion)
	//		if (subTimeTickRemaining > KINDA_SMALL_NUMBER)
	//		{
	//			const FVector NewVelocity = (Delta / subTimeTickRemaining);
	//			Velocity = NewVelocity;
	//		}

	//		if (subTimeTickRemaining > KINDA_SMALL_NUMBER && (Delta | Adjusted) > 0.f)
	//		{
	//			// Move in deflected direction.
	//			//SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
	//			SetPhysicalVelocity(Delta);
	//			if (Hit.bBlockingHit)
	//			{
	//				// hit second wall
	//				LastMoveTimeSlice = subTimeTickRemaining;
	//				subTimeTickRemaining = subTimeTickRemaining * (1.f - Hit.Time);

	//				if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
	//				{
	//					remainingTime += subTimeTickRemaining;
	//					ProcessLanded(Hit, remainingTime, Iterations);
	//					return;
	//				}

	//				HandleImpact(Hit, LastMoveTimeSlice, Delta);

	//				// If we've changed physics mode, abort.
	//				if (!HasValidData() || !IsFalling())
	//				{
	//					return;
	//				}

	//				// Act as if there was no air control on the last move when computing new deflection.
	//				if (bHasAirControl && Hit.Normal.Z > VERTICAL_SLOPE_NORMAL_Z)
	//				{
	//					const FVector LastMoveNoAirControl = VelocityNoAirControl * LastMoveTimeSlice;
	//					Delta = ComputeSlideVector(LastMoveNoAirControl, 1.f, OldHitNormal, Hit);
	//				}

	//				FVector PreTwoWallDelta = Delta;
	//				TwoWallAdjust(Delta, Hit, OldHitNormal);

	//				// Limit air control, but allow a slide along the second wall.
	//				if (bHasAirControl)
	//				{
	//					const bool bCheckLandingSpot = false; // we already checked above.
	//					const FVector AirControlDeltaV = LimitAirControl(subTimeTickRemaining, AirControlAccel, Hit, bCheckLandingSpot) * subTimeTickRemaining;

	//					// Only allow if not back in to first wall
	//					if (FVector::DotProduct(AirControlDeltaV, OldHitNormal) > 0.f)
	//					{
	//						Delta += (AirControlDeltaV * subTimeTickRemaining);
	//					}
	//				}

	//				// Compute velocity after deflection (only gravity component for RootMotion)
	//				if (subTimeTickRemaining > KINDA_SMALL_NUMBER)
	//				{
	//					const FVector NewVelocity = (Delta / subTimeTickRemaining);
	//					Velocity = NewVelocity;
	//				}

	//				// bDitch=true means that pawn is straddling two slopes, neither of which he can stand on
	//				bool bDitch = ((OldHitImpactNormal.Z > 0.f) && (Hit.ImpactNormal.Z > 0.f) && (FMath::Abs(Delta.Z) <= KINDA_SMALL_NUMBER) && ((Hit.ImpactNormal | OldHitImpactNormal) < 0.f));
	//				//SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
	//				SetPhysicalVelocity(Delta);
	//				if (Hit.Time == 0.f)
	//				{
	//					// if we are stuck then try to side step
	//					FVector SideDelta = (OldHitNormal + Hit.ImpactNormal).GetSafeNormal2D();
	//					if (SideDelta.IsNearlyZero())
	//					{
	//						SideDelta = FVector(OldHitNormal.Y, -OldHitNormal.X, 0).GetSafeNormal();
	//					}
	//					//SafeMoveUpdatedComponent(SideDelta, PawnRotation, true, Hit);
	//					SetPhysicalVelocity(SideDelta);
	//				}

	//				if (bDitch || IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit) || Hit.Time == 0.f)
	//				{
	//					remainingTime = 0.f;
	//					ProcessLanded(Hit, remainingTime, Iterations);
	//					return;
	//				}
	//				else if (Hit.Time == 1.f && OldHitImpactNormal.Z >= m_fWalkableFloorZ)//?
	//				{
	//					// We might be in a virtual 'ditch' within our perch radius. This is rare.
	//					const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
	//					const float ZMovedDist = FMath::Abs(PawnLocation.Z - OldLocation.Z);
	//					const float MovedDist2DSq = (PawnLocation - OldLocation).SizeSquared2D();
	//					if (ZMovedDist <= 0.2f * timeTick && MovedDist2DSq <= 4.f * timeTick)
	//					{
	//						Velocity.X += 0.25f * GetMaxSpeed() * (FMath::FRand() - 0.5f);
	//						Velocity.Y += 0.25f * GetMaxSpeed() * (FMath::FRand() - 0.5f);
	//						Velocity.Z = FMath::Max<float>(JumpZVelocity * 0.25f, 1.f);
	//						Delta = Velocity * timeTick;
	//						//SafeMoveUpdatedComponent(Delta, PawnRotation, true, Hit);
	//						SetPhysicalVelocity(Delta);
	//					}
	//				}
	//			}
	//		}
	//	}
	//}

	//if (Velocity.SizeSquared2D() <= KINDA_SMALL_NUMBER * 10.f)
	//{
	//	Velocity.X = 0.f;
	//	Velocity.Y = 0.f;
	//}
};

// 2066
void UUSBMovementComponent::PerformMovement(float DeltaSeconds)
{
	if (m_eCurrentMovementMode == MOVE_None || UpdatedComponent->Mobility != EComponentMobility::Movable)
	{
		// Clear pending physics forces
		ClearAccumulatedForces();
		return;
	}

	FVector OldVelocity;
	FVector OldLocation;

	// Scoped updates can improve performance of multiple MoveComponent calls.
		OldVelocity = Velocity;
		OldLocation = UpdatedComponent->GetComponentLocation();

		ApplyAccumulatedForces(DeltaSeconds);

		HandlePendingLaunch();
		ClearAccumulatedForces();

		// Clear jump input now, to allow movement events to trigger it for next update.
		ClearJumpInput(DeltaSeconds);

		// change position
		StartNewPhysics(DeltaSeconds);

		if (!HasValidData())
		{
			return;
		}


		PhysicsRotation(DeltaSeconds);
		// consume path following requested velocity
		m_bHasRequestedVelocity = false;

	UpdateComponentVelocity();

	const FVector NewLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const FQuat NewRotation = UpdatedComponent ? UpdatedComponent->GetComponentQuat() : FQuat::Identity;

	m_vLastUpdateLocation = NewLocation;
	m_qLastUpdateRotation = NewRotation;
	m_vLastUpdateVelocity = Velocity;

}

// 2754
void UUSBMovementComponent::StartNewPhysics(float deltaTime)
{
	if ((deltaTime < MIN_TICK_TIME) || !HasValidData())
	{
		return;
	}

	switch (m_eCurrentMovementMode)
	{
	case MOVE_None:
		break;
	case MOVE_Walking:
		PhysWalking(deltaTime);
		break;
	case MOVE_Falling:
		PhysFalling(deltaTime);
		break;
	default:
		SetMovementMode(MOVE_None);
		break;
	}
}

// 2805
float UUSBMovementComponent::GetGravityZ() const
{
	return (bEnableGravity ? (Super::GetGravityZ() * GravityScale) : 0.f);
}

// 2810
float UUSBMovementComponent::GetMaxSpeed() const
{
	switch (m_eCurrentMovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
		return MaxWalkSpeed;
	case MOVE_None:
	default:
		return 0.f;
	}
}

// 2831
float UUSBMovementComponent::GetMinAnalogSpeed() const
{
	switch (m_eCurrentMovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	case MOVE_Falling:
		return MinAnalogWalkSpeed;
	default:
		return 0.f;
	}
}

// 2873
float UUSBMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}
	PRINTF("SlideAlongSurface");

	FVector Normal(InNormal);
	if (IsMovingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (Normal.Z > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = Normal.GetSafeNormal2D();
			}
		}
		else if (Normal.Z < -KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (FloorNormal.Z < 1.f - DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}

				Normal = Normal.GetSafeNormal2D();
			}
		}
	}

	return Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}


// 2954
FVector UUSBMovementComponent::ComputeSlideVector(const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	FVector Result = Super::ComputeSlideVector(Delta, Time, Normal, Hit);

	// prevent boosting up slopes
	if (IsFalling())
	{
		Result = HandleSlopeBoosting(Result, Delta, Time, Normal, Hit);
	}

	return Result;
}

// 2968
FVector UUSBMovementComponent::HandleSlopeBoosting(const FVector& SlideResult, const FVector& Delta, const float Time, const FVector& Normal, const FHitResult& Hit) const
{
	FVector Result = SlideResult;

	if (Result.Z > 0.f)
	{
		// Don't move any higher than we originally intended.
		const float ZLimit = Delta.Z * Time;
		if (Result.Z - ZLimit > KINDA_SMALL_NUMBER)
		{
			if (ZLimit > 0.f)
			{
				// Rescale the entire vector (not just the Z component) otherwise we change the direction and likely head right back into the impact.
				const float UpPercent = ZLimit / Result.Z;
				Result *= UpPercent;
			}
			else
			{
				// We were heading down but were going to deflect upwards. Just make the deflection horizontal.
				Result = FVector::ZeroVector;
			}

			// Make remaining portion of original result horizontal and parallel to impact normal.
			const FVector RemainderXY = (SlideResult - Result) * FVector(1.f, 1.f, 0.f);
			const FVector NormalXY = Normal.GetSafeNormal2D();
			const FVector Adjust = Super::ComputeSlideVector(RemainderXY, 1.f, NormalXY, Hit);
			Result += Adjust;
		}
	}

	return Result;
}

// 3001
FVector UUSBMovementComponent::NewFallVelocity(const FVector& InitialVelocity, const FVector& Gravity, float DeltaTime) const
{
	FVector Result = InitialVelocity;

	if (DeltaTime > 0.f)
	{
		// Apply gravity.
		Result += Gravity * DeltaTime;

		// Don't exceed terminal velocity.
		const float TerminalLimit = FMath::Abs(GetPhysicsVolume()->TerminalVelocity);
		if (Result.SizeSquared() > FMath::Square(TerminalLimit))
		{
			const FVector GravityDir = Gravity.GetSafeNormal();
			if ((Result | GravityDir) > TerminalLimit)
			{
				Result = FVector::PointPlaneProject(Result, FVector::ZeroVector, GravityDir) + GravityDir * TerminalLimit;
			}
		}
	}

	return Result;
}

// 3062
bool UUSBMovementComponent::IsMovingOnGround() const
{
	return ((m_eCurrentMovementMode == MOVE_Walking) || (m_eCurrentMovementMode == MOVE_NavWalking)) && UpdatedComponent;
}

// 3067
bool UUSBMovementComponent::IsFalling() const
{
	return (m_eCurrentMovementMode == MOVE_Falling) && UpdatedComponent;
}

// 3082
void UUSBMovementComponent::CalcVelocity(float DeltaTime, float Friction, float BrakingDeceleration)
{
	if (!HasValidData() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	Friction = FMath::Max(0.f, Friction);//�ݵ�� ���
	const float MaxAccel = GetMaxAcceleration();
	float MaxSpeed = GetMaxSpeed();

	// Check if path following requested movement
	bool bZeroRequestedAcceleration = true;
	FVector RequestedAcceleration = FVector::ZeroVector;
	float RequestedSpeed = 0.0f;


	// Path following above didn't care about the analog modifier, but we do for everything else below, so get the fully modified value.
	// Use max of requested speed and max speed if we modified the speed in ApplyRequestedMove above.
	const float MaxInputSpeed = FMath::Max(MaxSpeed * m_fAnalogInputModifier, GetMinAnalogSpeed());
	MaxSpeed = FMath::Max(RequestedSpeed, MaxInputSpeed);


	// Apply braking or deceleration
	const bool bZeroAcceleration = m_vAcceleration.IsZero();
	const bool bVelocityOverMax = IsExceedingMaxSpeed(MaxSpeed);


	// Only apply braking if there is no acceleration, or we are over our max speed and need to slow down to it.
	if ((bZeroAcceleration && bZeroRequestedAcceleration) || bVelocityOverMax)
	{
		const FVector OldVelocity = Velocity;
		ApplyVelocityBraking(DeltaTime, Friction, BrakingDeceleration);

		// Don't allow braking to lower us below max speed if we started above it.
		if (bVelocityOverMax && Velocity.SizeSquared() < FMath::Square(MaxSpeed) && FVector::DotProduct(m_vAcceleration, OldVelocity) > 0.0f)
		{
			Velocity = (OldVelocity.GetSafeNormal()) * MaxSpeed;
		}
	}
	else if (!bZeroAcceleration)
	{
		// Friction affects our ability to change direction. This is only done for input acceleration, not path following.
		const FVector AccelDir = (m_vAcceleration.Size() == 0) ? FVector::ZeroVector : (m_vAcceleration.GetSafeNormal());
		const float VelSize = Velocity.Size();
		Velocity = Velocity - (Velocity - AccelDir * VelSize) * FMath::Min(DeltaTime * Friction, 1.f);
	}


	// Apply input acceleration
	if (!bZeroAcceleration)
	{
		const float NewMaxInputSpeed = IsExceedingMaxSpeed(MaxInputSpeed) ? Velocity.Size() : MaxInputSpeed;
		Velocity += m_vAcceleration * DeltaTime;
		Velocity = Velocity.GetClampedToMaxSize(NewMaxInputSpeed);
	}

}

// 3179
bool UUSBMovementComponent::ApplyRequestedMove(float DeltaTime, float MaxAccel, float MaxSpeed, float Friction, float BrakingDeceleration, FVector& OutAcceleration, float& OutRequestedSpeed)
{
	if (m_bHasRequestedVelocity)
	{
		const float RequestedSpeedSquared = m_vRequestedVelocity.SizeSquared();
		if (RequestedSpeedSquared < KINDA_SMALL_NUMBER)
		{
			return false;
		}

		// Compute requested speed from path following
		float RequestedSpeed = FMath::Sqrt(RequestedSpeedSquared);
		const FVector RequestedMoveDir = m_vRequestedVelocity / RequestedSpeed;
		RequestedSpeed = (m_bRequestedMoveWithMaxSpeed ? MaxSpeed : FMath::Min(MaxSpeed, RequestedSpeed));

		// Compute actual requested velocity
		const FVector MoveVelocity = RequestedMoveDir * RequestedSpeed;

		// Compute acceleration. Use MaxAccel to limit speed increase, 1% buffer.
		FVector NewAcceleration = FVector::ZeroVector;
		const float CurrentSpeedSq = Velocity.SizeSquared();
		if (m_bRequestedMoveUseAcceleration && CurrentSpeedSq < FMath::Square(RequestedSpeed * 1.01f))
		{
			// Turn in the same manner as with input acceleration.
			const float VelSize = FMath::Sqrt(CurrentSpeedSq);
			Velocity = Velocity - (Velocity - RequestedMoveDir * VelSize) * FMath::Min(DeltaTime * Friction, 1.f);

			// How much do we need to accelerate to get to the new velocity?
			NewAcceleration = ((MoveVelocity - Velocity) / DeltaTime);
			NewAcceleration = NewAcceleration.GetClampedToMaxSize(MaxAccel);
		}
		else
		{
			// Just set velocity directly.
			// If decelerating we do so instantly, so we don't slide through the destination if we can't brake fast enough.
			Velocity = MoveVelocity;
		}

		// Copy to out params
		OutRequestedSpeed = RequestedSpeed;
		OutAcceleration = NewAcceleration;
		return true;
	}

	return false;
}

// 3503
float UUSBMovementComponent::GetMaxJumpHeight() const
{
	const float Gravity = GetGravityZ();
	if (FMath::Abs(Gravity) > KINDA_SMALL_NUMBER)
	{
		return FMath::Square(JumpZVelocity) / (-2.f * Gravity);
	}
	else
	{
		return 0.f;
	}
}

// 3516
float UUSBMovementComponent::GetMaxJumpHeightWithJumpTime() const
{
	const float MaxJumpHeight = GetMaxJumpHeight();

	if (PawnOwner)
	{
		// When bApplyGravityWhileJumping is true, the actual max height will be lower than this.
		// However, it will also be dependent on framerate (and substep iterations) so just return this
		// to avoid expensive calculations.

		// This can be imagined as the character being displaced to some height, then jumping from that height.
		return (JumpMaxHoldTime * JumpZVelocity) + MaxJumpHeight;
	}

	return MaxJumpHeight;
}

// 3552
float UUSBMovementComponent::GetMaxAcceleration() const
{
	return MaxAcceleration;
}

// 3557
float UUSBMovementComponent::GetMaxBrakingDeceleration() const
{
	switch (m_eCurrentMovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
		return BrakingDecelerationWalking;
	case MOVE_Falling:
		return BrakingDecelerationFalling;
	case MOVE_None:
	default:
		return 0.f;
	}
}

// 3578
FVector UUSBMovementComponent::GetCurrentAcceleration() const
{
	return m_vAcceleration;
}

// 3583
void UUSBMovementComponent::ApplyVelocityBraking(float DeltaTime, float Friction, float BrakingDeceleration)
{
	if (Velocity.IsZero() || !HasValidData() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	const float FrictionFactor = FMath::Max(0.f, BrakingFrictionFactor);
	Friction = FMath::Max(0.f, Friction * FrictionFactor);
	BrakingDeceleration = FMath::Max(0.f, BrakingDeceleration);
	const bool bZeroFriction = (Friction == 0.f);
	const bool bZeroBraking = (BrakingDeceleration == 0.f);

	if (bZeroFriction && bZeroBraking)
	{
		return;
	}

	const FVector OldVel = Velocity;

	// Decelerate to brake to a stop
	const FVector RevAccel = bZeroBraking ? FVector::ZeroVector : -BrakingDeceleration * Velocity.GetSafeNormal();

	// apply friction and braking
	Velocity = Velocity + ((-Friction) * Velocity + RevAccel);

	// Don't reverse direction
	if ((Velocity | OldVel) <= 0.f)
	{
		Velocity = FVector::ZeroVector;
		return;
	}

	// Clamp to zero if nearly zero, or if below min threshold and braking.
	const float VSizeSq = Velocity.SizeSquared();
	if (VSizeSq <= KINDA_SMALL_NUMBER || (!bZeroBraking && VSizeSq <= FMath::Square(BRAKE_TO_STOP_VELOCITY)))
	{
		Velocity = FVector::ZeroVector;
	}
}

// 4008
FVector UUSBMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{//���߿��� �̵� ����
	// No acceleration in Z
	FVector FallAcceleration = FVector(m_vAcceleration.X, m_vAcceleration.Y, 0.f);

	// bound acceleration, falling object has minimal ability to impact acceleration
	if (FallAcceleration.SizeSquared2D() > 0.f)
	{
		FallAcceleration = GetAirControl(DeltaTime, m_fAirControl, FallAcceleration);
		FallAcceleration = FallAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	}

	return FallAcceleration;
}

// 4024
FVector UUSBMovementComponent::GetAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	// Boost
	if (TickAirControl != 0.f)
	{
		TickAirControl = BoostAirControl(DeltaTime, TickAirControl, FallAcceleration);
	}

	return TickAirControl * FallAcceleration;
}

// 4036
float UUSBMovementComponent::BoostAirControl(float DeltaTime, float TickAirControl, const FVector& FallAcceleration)
{
	// Allow a burst of initial acceleration
	if (AirControlBoostMultiplier > 0.f && Velocity.SizeSquared2D() < FMath::Square(AirControlBoostVelocityThreshold))
	{
		TickAirControl = FMath::Min(1.f, AirControlBoostMultiplier * TickAirControl);
	}

	return TickAirControl;
}


// 4320
FVector UUSBMovementComponent::LimitAirControl(float DeltaTime, const FVector& FallAcceleration, const FHitResult& HitResult, bool bCheckForValidLandingSpot)
{
	FVector Result(FallAcceleration);

	if (HitResult.IsValidBlockingHit() && HitResult.Normal.Z > VERTICAL_SLOPE_NORMAL_Z)
	{
		if (!bCheckForValidLandingSpot || !IsValidLandingSpot(HitResult.Location, HitResult))
		{
			// If acceleration is into the wall, limit contribution.
			if (FVector::DotProduct(FallAcceleration, HitResult.Normal) < 0.f)
			{
				// Allow movement parallel to the wall, but not into it because that may push us up.
				const FVector Normal2D = HitResult.Normal.GetSafeNormal2D();
				Result = FVector::VectorPlaneProject(FallAcceleration, Normal2D);
			}
		}
	}
	else if (HitResult.bStartPenetrating)
	{
		// Allow movement out of penetration.
		return (FVector::DotProduct(Result, HitResult.Normal) > 0.f ? Result : FVector::ZeroVector);
	}

	return Result;
}


bool UUSBMovementComponent::CheckFall(const FUSBFindFloorResult & OldFloor, const FHitResult & Hit, const FVector & Delta, const FVector & OldLocation, float timeTick, bool bMustJump)
{
	if (!HasValidData())
	{
		return false;
	}

	if (bMustJump)
	{
		PRINTF("Must Jump 1");
		if (IsMovingOnGround())
		{
			PRINTF("Must Jump 2");
			// If still walking, then fall. If not, assume the user set a different mode they want to keep.
			StartFalling(timeTick, Delta, OldLocation);
		}
		return true;
	}
	PRINTF("Must Jump 3");
	return false;
}

// 4456
void UUSBMovementComponent::RevertMove(const FVector& OldLocation, bool bFailMove)
{
	UpdatedComponent->SetWorldLocation(OldLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (bFailMove)
	{
		// end movement now
		Velocity = FVector::ZeroVector;
		m_vAcceleration = FVector::ZeroVector;
	}
}

// 4489
FVector UUSBMovementComponent::ComputeGroundMovementDelta(const FVector& Delta, const FHitResult& RampHit, const bool bHitFromLineTrace) const
{
	const FVector FloorNormal = RampHit.ImpactNormal;
	const FVector ContactNormal = RampHit.Normal;

	if (FloorNormal.Z < (1.f - KINDA_SMALL_NUMBER) && FloorNormal.Z > KINDA_SMALL_NUMBER&& ContactNormal.Z > KINDA_SMALL_NUMBER && !bHitFromLineTrace && IsWalkable(RampHit))
	{
		// Compute a vector that moves parallel to the surface, by projecting the horizontal movement direction onto the ramp.
		const float FloorDotDelta = (FloorNormal | Delta);
		FVector RampMovement(Delta.X, Delta.Y, -FloorDotDelta / FloorNormal.Z);

		return RampMovement.GetSafeNormal() * Delta.Size();
	}

	return Delta;
}

// 4552
//���� �����̴°�
void UUSBMovementComponent::MoveAlongFloor(const FVector& InVelocity, float DeltaSeconds)
{
	// Move along the current floor
	const FVector Delta = FVector(InVelocity.X, InVelocity.Y, 0.f) * DeltaSeconds;
	FHitResult Hit(1.f);
	FVector RampVector = ComputeGroundMovementDelta(Delta, CurrentFloor.HitResult, CurrentFloor.bLineTrace);
	//SafeMoveUpdatedComponent(RampVector, UpdatedComponent->GetComponentQuat(), true, Hit);
	SetPhysicalVelocity(RampVector,&Hit);//need delta
	float LastMoveTimeSlice = DeltaSeconds;

	if (Hit.bStartPenetrating)	// SafeMoveUpdatedComponent�� ���� �̵��� ����� �ٸ� �ݸ����̶� ��ģ ��Ȳ�̶��
	{
		// Allow this hit to be used as an impact we can deflect off, otherwise we do nothing the rest of the update and appear to hitch.
		SlideAlongSurface(Delta, 1.f, Hit.Normal, Hit, true);

		if (Hit.bStartPenetrating)
		{
			//OnCharacterStuckInGeometry(&Hit);	// ������ �� bJustTeleported = true ���� ���� ����
		}
	}
	else if (Hit.IsValidBlockingHit())	// �Ϲ����� BlockingHit�� ��
	{
		// We impacted something (most likely another ramp, but possibly a barrier).
		float PercentTimeApplied = Hit.Time;//���ۿ��� ������ ��� �΋H�������� �ۼ�Ʈ
		if ((Hit.Time > 0.f) && (Hit.Normal.Z > KINDA_SMALL_NUMBER) && IsWalkable(Hit))	// Hit�� ������ �������� �����ȴٸ� ������ ���� ����
		{
			// Another walkable ramp.
			const float InitialPercentRemaining = 1.f - PercentTimeApplied;
			RampVector = ComputeGroundMovementDelta(Delta * InitialPercentRemaining, Hit, false);
			LastMoveTimeSlice = InitialPercentRemaining * LastMoveTimeSlice;
			SetPhysicalVelocity(RampVector,&Hit);
			const float SecondHitPercent = Hit.Time * InitialPercentRemaining;
			PercentTimeApplied = FMath::Clamp(PercentTimeApplied + SecondHitPercent, 0.f, 1.f);
		}

		//������ ������ �� �΋H������ Ȯ��

		if (Hit.IsValidBlockingHit())	// Hit ���ο� ���� ��� �̵����� ����
		{
			SlideAlongSurface(Delta, 1.f - PercentTimeApplied, Hit.Normal, Hit, true);
		}
	}
}

// 4622
void UUSBMovementComponent::MaintainHorizontalGroundVelocity()
{
	if (Velocity.Z != 0.f)
	{
		Velocity = Velocity.GetSafeNormal2D() * Velocity.Size();
	}
}

void UUSBMovementComponent::ProcessLanded(const FHitResult & Hit, float remainingTime)
{
	if (IsFalling())
	{
		SetPostLandedPhysics(Hit);
	}

	StartNewPhysics(remainingTime);
}
// 5317
void UUSBMovementComponent::SetPostLandedPhysics(const FHitResult& Hit)
{
	if (PawnOwner)
	{
		const FVector PreImpactAccel = m_vAcceleration + (IsFalling() ? FVector(0.f, 0.f, GetGravityZ()) : FVector::ZeroVector);
		const FVector PreImpactVelocity = Velocity;

		if (m_eDefaultLandMovementMode == MOVE_Walking ||
			m_eDefaultLandMovementMode == MOVE_NavWalking ||
			m_eDefaultLandMovementMode == MOVE_Falling)
		{
			SetMovementMode(m_eDefaultGroundMovementMode);
		}
		else
		{
			SetDefaultMovementMode();
		}
	}
}
// 5415
void UUSBMovementComponent::OnTeleported()
{
	UE_LOG(LogTemp, Log, TEXT("OnTeleported"));
	if (!HasValidData())
	{
		return;
	}

	Super::OnTeleported();

	// Find floor at current location
	UpdateFloorFromAdjustment();

	// Validate it. We don't want to pop down to walking mode from very high off the ground, but we'd like to keep walking if possible.
	UPrimitiveComponent* OldBase = PawnOwner->GetMovementBase();
	UPrimitiveComponent* NewBase = NULL;

	if (OldBase && CurrentFloor.IsWalkableFloor() && CurrentFloor.FloorDist <= MAX_FLOOR_DIST && Velocity.Z <= 0.f)
	{
		// Close enough to land or just keep walking.
		NewBase = CurrentFloor.HitResult.Component.Get();
	}
	else
	{
		CurrentFloor.Clear();
	}

	const bool bWasFalling = (m_eCurrentMovementMode == MOVE_Falling);

	if (!CurrentFloor.IsWalkableFloor() || (OldBase && !NewBase))
	{
		if (!bWasFalling && m_eCurrentMovementMode != MOVE_Flying && m_eCurrentMovementMode != MOVE_Custom)
		{
			SetMovementMode(MOVE_Falling);
		}
	}
	else if (NewBase)
	{
		if (bWasFalling)
		{
			ProcessLanded(CurrentFloor.HitResult, 0.f);
		}
	}
}

// 5475
float GetAxisDeltaRotation(float InAxisRotationRate, float DeltaTime)
{
	return (InAxisRotationRate >= 0.f) ? (InAxisRotationRate * DeltaTime) : 360.f;
}

// 5480
FRotator UUSBMovementComponent::GetDeltaRotation(float DeltaTime) const
{
	return FRotator(GetAxisDeltaRotation(RotationRate.Pitch, DeltaTime), GetAxisDeltaRotation(RotationRate.Yaw, DeltaTime), GetAxisDeltaRotation(RotationRate.Roll, DeltaTime));
}

// 5485
FRotator UUSBMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const
{
	if (m_vAcceleration.SizeSquared() < KINDA_SMALL_NUMBER)
	{
		// AI path following request can orient us in that direction (it's effectively an acceleration)
		if (m_bHasRequestedVelocity && m_vRequestedVelocity.SizeSquared() > KINDA_SMALL_NUMBER)
		{
			return m_vRequestedVelocity.GetSafeNormal().Rotation();
		}

		// Don't change rotation if there is no acceleration.
		return CurrentRotation;
	}

	// Rotate toward direction of acceleration.
	return m_vAcceleration.GetSafeNormal().Rotation();
}

// 5503
bool UUSBMovementComponent::ShouldRemainVertical() const
{
	// Always remain vertical when walking or falling.
	return IsMovingOnGround() || IsFalling();
}

// 5509
void UUSBMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!HasValidData() || (!PawnOwner->Controller))
	{
		return;
	}

	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized

	FRotator DeltaRot = GetDeltaRotation(DeltaTime);

	FRotator DesiredRotation = CurrentRotation;

	DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRot);

	if (ShouldRemainVertical())
	{
		DesiredRotation.Pitch = 0.f;
		DesiredRotation.Yaw = FRotator::NormalizeAxis(DesiredRotation.Yaw);
		DesiredRotation.Roll = 0.f;
	}
	else
	{
		DesiredRotation.Normalize();
	}

	// Accumulate a desired new rotation.
	const float AngleTolerance = 1e-3f;

	if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
	{
		// PITCH
		if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
		{
			DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
		}

		// YAW
		if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
		{
			DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
		}

		// ROLL
		if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
		{
			DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
		}

		MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, /*bSweep*/ false,nullptr,ETeleportType::TeleportPhysics);
	}
}

// 5681
void UUSBMovementComponent::AddImpulse(FVector Impulse, bool bVelocityChange)
{
	if (!Impulse.IsZero() && (m_eCurrentMovementMode != MOVE_None) && IsActive() && HasValidData())
	{
		// handle scaling by mass
		FVector FinalImpulse = Impulse;
		if (!bVelocityChange)
		{
			if (m_fMass > SMALL_NUMBER)
			{
				FinalImpulse = FinalImpulse / m_fMass;
			}
		}

		m_vPendingImpulseToApply += FinalImpulse;
	}
}

// 5703
void UUSBMovementComponent::AddForce(FVector Force)
{
	if (!Force.IsZero() && (m_eCurrentMovementMode != MOVE_None) && IsActive() && HasValidData())
	{
		if (m_fMass > SMALL_NUMBER)
		{
			m_vPendingForceToApply += Force / m_fMass;
		}
	}
}

// 5790
bool UUSBMovementComponent::IsWalkable(const FHitResult& Hit) const
{
	if (!Hit.IsValidBlockingHit())
	{
		// No hit, or starting in penetration
		return false;
	}

	// Never walk up vertical surfaces.
	if (Hit.ImpactNormal.Z < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	float TestWalkableZ = m_fWalkableFloorZ;

	// See if this component overrides the walkable floor z.
	const UPrimitiveComponent* HitComponent = Hit.Component.Get();
	if (HitComponent)
	{
		const FWalkableSlopeOverride& SlopeOverride = HitComponent->GetWalkableSlopeOverride();
		TestWalkableZ = SlopeOverride.ModifyWalkableFloorZ(TestWalkableZ);
	}

	// Can't walk on this surface if it is too steep.
	if (Hit.ImpactNormal.Z < TestWalkableZ)
	{
		return false;
	}

	return true;
}

// 5823
void UUSBMovementComponent::SetWalkableFloorAngle(float InWalkableFloorAngle)
{
	m_fWalkableFloorAngle = FMath::Clamp(InWalkableFloorAngle, 0.f, 90.0f);
	m_fWalkableFloorZ = FMath::Cos(FMath::DegreesToRadians(m_fWalkableFloorAngle));
}

// 5829
void UUSBMovementComponent::SetWalkableFloorZ(float InWalkableFloorZ)
{
	m_fWalkableFloorZ = FMath::Clamp(InWalkableFloorZ, 0.f, 1.f);
	m_fWalkableFloorAngle = FMath::RadiansToDegrees(FMath::Acos(m_fWalkableFloorZ));
}

// 5847
bool UUSBMovementComponent::IsWithinEdgeTolerance(const FVector& CapsuleLocation, const FVector& TestImpactPoint, const float CapsuleRadius) const
{
	const float DistFromCenterSq = (TestImpactPoint - CapsuleLocation).SizeSquared2D();
	const float ReducedRadiusSq = FMath::Square(FMath::Max(SWEEP_EDGE_REJECT_DISTANCE + KINDA_SMALL_NUMBER, CapsuleRadius - SWEEP_EDGE_REJECT_DISTANCE));
	return DistFromCenterSq < ReducedRadiusSq;
}
// 5999
void UUSBMovementComponent::FindFloor(const FVector& CapsuleLocation, FUSBFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	// No collision, no floor...
	if (!HasValidData() || !UpdatedComponent->IsQueryCollisionEnabled())
	{
		OutFloorResult.Clear();
		return;
	}
	// Increase height check slightly if walking, to prevent floor height adjustment from later invalidating the floor result.
	// �ٴ��� ���� ������ Floor Result�� ��ȿȭ �Ǵ°� �����ϱ� ���� Walking���¿����� ���̸� ���麸�� �ణ �����д�.
	const float HeightCheckAdjust = (IsMovingOnGround() ? MAX_FLOOR_DIST + KINDA_SMALL_NUMBER : -MAX_FLOOR_DIST);

	float FloorSweepTraceDist = FMath::Max(MAX_FLOOR_DIST, HeightCheckAdjust);
	float FloorLineTraceDist = FloorSweepTraceDist;
	bool bNeedToValidateFloor = true;

	// Sweep floor
	if (FloorLineTraceDist > 0.f || FloorSweepTraceDist > 0.f)
	{
		if (!bCanUseCachedLocation)
		{
			ComputeFloorDist(CapsuleLocation, FloorLineTraceDist, FloorSweepTraceDist, OutFloorResult, GetBoundingCapsule()->GetScaledCapsuleRadius(), DownwardSweepResult);
		}
		else
		{
			// Force floor check if base has collision disabled or if it does not block us.
			const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

			ComputeFloorDist(CapsuleLocation, FloorLineTraceDist, FloorSweepTraceDist, OutFloorResult, GetBoundingCapsule()->GetScaledCapsuleRadius(), DownwardSweepResult);
		}
	}
}

void UUSBMovementComponent::ComputeFloorDist(const FVector & CapsuleLocation, float LineDistance, float SweepDistance, FUSBFindFloorResult & OutFloorResult, float SweepRadius, const FHitResult * DownwardSweepResult) const
{
	OutFloorResult.Clear();

	float PawnRadius, PawnHalfHeight;	// ������ Capsule ���
	GetBoundingCapsule()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

	bool bSkipSweep = false;
	if (DownwardSweepResult != NULL && DownwardSweepResult->IsValidBlockingHit())
	{
		// Only if the supplied sweep was vertical and downward.
		if ((DownwardSweepResult->TraceStart.Z > DownwardSweepResult->TraceEnd.Z) &&
			(DownwardSweepResult->TraceStart - DownwardSweepResult->TraceEnd).SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			// Reject hits that are barely on the cusp of the radius of the capsule
			if (IsWithinEdgeTolerance(DownwardSweepResult->Location, DownwardSweepResult->ImpactPoint, PawnRadius))
			{
				// Don't try a redundant sweep, regardless of whether this sweep is usable.
				bSkipSweep = true;

				const bool bIsWalkable = IsWalkable(*DownwardSweepResult);
				const float FloorDist = (CapsuleLocation.Z - DownwardSweepResult->Location.Z);
				OutFloorResult.SetFromSweep(*DownwardSweepResult, FloorDist, bIsWalkable);

				if (bIsWalkable)
				{
					// Use the supplied downward sweep as the floor hit result.			
					return;
				}
			}
		}
	}

	// We require the sweep distance to be >= the line distance, otherwise the HitResult can't be interpreted as the sweep result.
	if (SweepDistance < LineDistance)
	{
		ensure(SweepDistance >= LineDistance);
		return;
	}

	bool bBlockingHit = false;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ComputeFloorDist), false, PawnOwner);
	FCollisionResponseParams ResponseParam;
	InitCollisionParams(QueryParams, ResponseParam);
	const ECollisionChannel CollisionChannel = UpdatedComponent->GetCollisionObjectType();

	// Sweep test
	if (!bSkipSweep && SweepDistance > 0.f && SweepRadius > 0.f)
	{
		// Use a shorter height to avoid sweeps giving weird results if we start on a surface.
		// This also allows us to adjust out of penetrations.
		const float ShrinkScale = 0.9f;
		const float ShrinkScaleOverlap = 0.1f;
		float ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScale);
		float TraceDist = SweepDistance + ShrinkHeight;
		FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(SweepRadius, PawnHalfHeight - ShrinkHeight);

		FHitResult Hit(1.f);
		bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + FVector(0.f, 0.f, -TraceDist), CollisionChannel, SweepRadius, PawnHalfHeight - ShrinkHeight, QueryParams, ResponseParam);

		if (bBlockingHit)
		{
			// Reject hits adjacent to us, we only care about hits on the bottom portion of our capsule.
			// Check 2D distance to impact point, reject if within a tolerance from radius.
			if (Hit.bStartPenetrating || !IsWithinEdgeTolerance(CapsuleLocation, Hit.ImpactPoint, CapsuleShape.Capsule.Radius))
			{
				// Use a capsule with a slightly smaller radius and shorter height to avoid the adjacent object.
				// Capsule must not be nearly zero or the trace will fall back to a line trace from the start point and have the wrong length.
				CapsuleShape.Capsule.Radius = FMath::Max(0.f, CapsuleShape.Capsule.Radius - SWEEP_EDGE_REJECT_DISTANCE - KINDA_SMALL_NUMBER);
				if (!CapsuleShape.IsNearlyZero())
				{
					ShrinkHeight = (PawnHalfHeight - PawnRadius) * (1.f - ShrinkScaleOverlap);
					TraceDist = SweepDistance + ShrinkHeight;
					CapsuleShape.Capsule.HalfHeight = FMath::Max(PawnHalfHeight - ShrinkHeight, CapsuleShape.Capsule.Radius);
					Hit.Reset(1.f, false);

					bBlockingHit = FloorSweepTest(Hit, CapsuleLocation, CapsuleLocation + FVector(0.f, 0.f, -TraceDist), CollisionChannel, SweepRadius, PawnHalfHeight - ShrinkHeight, QueryParams, ResponseParam);
				}
			}

			// Reduce hit distance by ShrinkHeight because we shrank the capsule for the trace.
			// We allow negative distances here, because this allows us to pull out of penetrations.
			const float MaxPenetrationAdjust = FMath::Max(MAX_FLOOR_DIST, PawnRadius);
			const float SweepResult = FMath::Max(-MaxPenetrationAdjust, Hit.Time * TraceDist - ShrinkHeight);

			OutFloorResult.SetFromSweep(Hit, SweepResult, false);
			if (Hit.IsValidBlockingHit() && IsWalkable(Hit))
			{
				if (SweepResult <= SweepDistance)
				{
					// Hit within test distance.
					OutFloorResult.bWalkableFloor = true;
					return;
				}
			}
		}
	}


	// No hits were acceptable.
	OutFloorResult.bWalkableFloor = false;
	OutFloorResult.FloorDist = SweepDistance;
}

// 6161
bool UUSBMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return false;
	}

	// Skip some checks if penetrating. Penetration will be handled by the FindFloor call (using a smaller capsule)
	if (!Hit.bStartPenetrating)
	{
		// Reject unwalkable floor normals.
		if (!IsWalkable(Hit))
		{
			return false;
		}

		float PawnRadius, PawnHalfHeight;
		GetBoundingCapsule()->GetScaledCapsuleSize(PawnRadius, PawnHalfHeight);

		// Reject hits that are above our lower hemisphere (can happen when sliding down a vertical surface).
		const float LowerHemisphereZ = Hit.Location.Z - PawnHalfHeight + PawnRadius;
		if (Hit.ImpactPoint.Z >= LowerHemisphereZ)
		{
			return false;
		}

		// Reject hits that are barely on the cusp of the radius of the capsule
		if (!IsWithinEdgeTolerance(Hit.Location, Hit.ImpactPoint, PawnRadius))
		{
			return false;
		}
	}
	else
	{
		// Penetrating
		if (Hit.Normal.Z < KINDA_SMALL_NUMBER)
		{
			// Normal is nearly horizontal or downward, that's a penetration adjustment next to a vertical or overhanging wall. Don't pop to the floor.
			return false;
		}
	}

	FUSBFindFloorResult FloorResult;
	FindFloor(CapsuleLocation, FloorResult, false, &Hit);

	if (!FloorResult.IsWalkableFloor())
	{
		return false;
	}

	return true;
}

// 6123
bool UUSBMovementComponent::FloorSweepTest(FHitResult& OutHit,const FVector& Start,const FVector& End,ECollisionChannel TraceChannel, float radius, float halfHeight,const struct FCollisionQueryParams& Params,const struct FCollisionResponseParams& ResponseParam) const
{
	bool bBlockingHit = false;

	const FCollisionShape BoxShape = FCollisionShape::MakeBox(FVector(radius * 0.707f, radius * 0.707f, halfHeight));

	// First test with the box rotated so the corners are along the major axes (ie rotated 45 degrees).
	bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat(FVector(0.f, 0.f, -1.f), PI * 0.25f), TraceChannel, BoxShape, Params, ResponseParam);

	if (!bBlockingHit)
	{
		// Test again with the same box, not rotated.
		OutHit.Reset(1.f, false);
		bBlockingHit = GetWorld()->SweepSingleByChannel(OutHit, Start, End, FQuat::Identity, TraceChannel, BoxShape, Params, ResponseParam);
	}

	return bBlockingHit;
}

// 6215
bool UUSBMovementComponent::ShouldCheckForValidLandingSpot(float DeltaTime, const FVector& Delta, const FHitResult& Hit) const
{
	// See if we hit an edge of a surface on the lower portion of the capsule.
	// In this case the normal will not equal the impact normal, and a downward sweep may find a walkable surface on top of the edge.
	if (Hit.Normal.Z > KINDA_SMALL_NUMBER && !Hit.Normal.Equals(Hit.ImpactNormal))
	{
		const FVector PawnLocation = UpdatedComponent->GetComponentLocation();
		if (IsWithinEdgeTolerance(PawnLocation, Hit.ImpactPoint, GetBoundingCapsule()->GetScaledCapsuleRadius()))
		{
			return true;
		}
	}

	return false;
}

// 6821
FVector UUSBMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
	// walking or falling pawns ignore up/down sliding
	if (InputAcceleration.Z != 0.f && (IsMovingOnGround() || IsFalling()))
	{
		return FVector(InputAcceleration.X, InputAcceleration.Y, 0.f);
	}

	return InputAcceleration;
}

// 6833
void UUSBMovementComponent::ScaleInputAcceleration(const FVector& InputAcceleration)
{
	m_vInputNormal = InputAcceleration.GetClampedToMaxSize(1.f);
	m_vAcceleration= GetMaxAcceleration() * m_vInputNormal;
}

// 6849
float UUSBMovementComponent::ComputeAnalogInputModifier() const
{
	const float MaxAccel = GetMaxAcceleration();
	if (m_vAcceleration.SizeSquared() > 0.f && MaxAccel > SMALL_NUMBER)
	{
		return FMath::Clamp(m_vAcceleration.Size() / MaxAccel, 0.f, 1.f);
	}

	return 0.f;
}

// 6860
float UUSBMovementComponent::GetAnalogInputModifier() const
{
	return m_fAnalogInputModifier;
}


// 8843
void UUSBMovementComponent::UpdateFloorFromAdjustment()
{//�ڷ���Ʈ ���� �Ҹ�
	if (!HasValidData())
	{
		return;
	}

	FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, false);
}

// 9357,������ȣ�ۿ�,������ ���� ������Ʈ�� �������

// 9563
void UUSBMovementComponent::ApplyAccumulatedForces(float DeltaSeconds)
{
	// ����� �Է°� ������ �־��� ���� Velocity�� ������. ex) AddForces
	if (m_vPendingImpulseToApply.Z != 0.f || m_vPendingForceToApply.Z != 0.f)
	{
		// check to see if applied momentum is enough to overcome gravity
		if (IsMovingOnGround() && (m_vPendingImpulseToApply.Z + (m_vPendingForceToApply.Z * DeltaSeconds) + (GetGravityZ() * DeltaSeconds) > SMALL_NUMBER))
		{
			SetMovementMode(MOVE_Falling);
		}
	}

	Velocity += m_vPendingImpulseToApply + (m_vPendingForceToApply * DeltaSeconds);

	// Don't call ClearAccumulatedForces() because it could affect launch velocity
	m_vPendingImpulseToApply = FVector::ZeroVector;
	m_vPendingForceToApply = FVector::ZeroVector;
}

// 9581
void UUSBMovementComponent::ClearAccumulatedForces()
{
	m_vPendingImpulseToApply = FVector::ZeroVector;
	m_vPendingForceToApply = FVector::ZeroVector;
	m_vPendingLaunchVelocity = FVector::ZeroVector;
}



void UUSBMovementComponent::Jump()
{
	bPressedJump = true;
	JumpKeyHoldTime = 0.0f;
}

void UUSBMovementComponent::StopJumping()
{
	bPressedJump = false;
	ResetJumpState();
}

void UUSBMovementComponent::CheckJumpInput(float DeltaTime)
{
	if (bPressedJump)
	{
		// If this is the first jump and we're already falling,
		// then increment the JumpCount to compensate.
		const bool bFirstJump = JumpCurrentCount == 0;
		if (bFirstJump && IsFalling())
		{
			JumpCurrentCount++;
		}
		const bool bDidJump = CanJump() && DoJump();
		if (bDidJump)
		{
			// Transition from not (actively) jumping to jumping.
			if (!bWasJumping)
			{
				JumpCurrentCount++;
				JumpForceTimeRemaining = GetJumpMaxHoldTime();
				//OnJumped();
			}
		}
		bWasJumping = bDidJump;
	}
}

void UUSBMovementComponent::ClearJumpInput(float DeltaTime)
{
	if (bPressedJump)
	{
		JumpKeyHoldTime += DeltaTime;

		// Don't disable bPressedJump right away if it's still held.
		// Don't modify JumpForceTimeRemaining because a frame of update may be remaining.
		if (JumpKeyHoldTime >= GetJumpMaxHoldTime())
		{
			bPressedJump = false;
		}
	}
	else
	{
		JumpForceTimeRemaining = 0.0f;
		bWasJumping = false;
	}
}

float UUSBMovementComponent::GetJumpMaxHoldTime() const
{
	return JumpMaxHoldTime;
}

bool UUSBMovementComponent::CanJump() const
{
	// Ensure the character isn't currently crouched.
	bool bCanJump = true;// = !bIsCrouched;

	// Ensure that the CharacterMovement state is valid
	bCanJump &= IsJumpAllowed() &&
		// Can only jump from the ground, or multi-jump if already falling.
		(IsMovingOnGround() || IsFalling());

	if (bCanJump)
	{
		// Ensure JumpHoldTime and JumpCount are valid.
		if (!bWasJumping || GetJumpMaxHoldTime() <= 0.0f)
		{
			//UE_LOG(LogTemp, Log, TEXT("Before Process : %s"), bCanJump ? TEXT("True") : TEXT("False"));
			if (JumpCurrentCount == 0 && IsFalling())
			{
				bCanJump = JumpCurrentCount + 1 < JumpMaxCount;
			}
			else
			{
				bCanJump = JumpCurrentCount < JumpMaxCount;
			}
		}
		else
		{
			// Only consider JumpKeyHoldTime as long as:
			// A) The jump limit hasn't been met OR
			// B) The jump limit has been met AND we were already jumping
			const bool bJumpKeyHeld = (bPressedJump && JumpKeyHoldTime < GetJumpMaxHoldTime());
			bCanJump = bJumpKeyHeld &&
				((JumpCurrentCount < JumpMaxCount) || (bWasJumping && JumpCurrentCount == JumpMaxCount));
		}
	}

	return bCanJump;
}


void UUSBMovementComponent::ResetJumpState()
{
	bPressedJump = false;
	bWasJumping = false;
	JumpKeyHoldTime = 0.0f;
	JumpForceTimeRemaining = 0.0f;

	if (!IsFalling())
	{
		JumpCurrentCount = 0;
	}
}

bool UUSBMovementComponent::IsJumpProvidingForce() const
{
	if (JumpForceTimeRemaining > 0.0f)
	{
		return true;
	}

	return false;
}


void UUSBMovementComponent::SetPhysicalVelocity(FVector& velo,FHitResult* outSweep)
{
	if (PawnOwner)
	{
		auto* Prim = Cast<UPrimitiveComponent>(UpdatedComponent);

		//// Set up
		//if (Sweep(Prim, velo))
		{
			Prim->SetPhysicsLinearVelocity(velo);
			PRINTF("Velo : %s", *velo.ToString());
		}

	}
}

bool UUSBMovementComponent::Sweep(UPrimitiveComponent * Prim, FVector & velo)
{
	const FVector TraceStart = Prim->GetComponentLocation();
	const FVector TraceEnd = TraceStart + velo;
	float DeltaSizeSq = (TraceEnd - TraceStart).SizeSquared();				// Recalc here to account for precision loss of float addition
	const FQuat InitialRotationQuat = Prim->GetComponentTransform().GetRotation();

	// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
	const float MinMovementDistSq = FMath::Square(4.f*KINDA_SMALL_NUMBER);

	if (DeltaSizeSq <= MinMovementDistSq)//�ʹ������� ���� ���Ѵ�
	{
		// Skip if no vector or rotation.
		if (UpdatedComponent->GetComponentQuat().Equals(InitialRotationQuat, SCENECOMPONENT_QUAT_TOLERANCE))
		{
			return true;
		}
		DeltaSizeSq = 0.f;
	}

	// WARNING: HitResult is only partially initialized in some paths. All data is valid only if bFilledHitResult is true.
	FHitResult BlockingHit(NoInit);
	BlockingHit.bBlockingHit = false;
	BlockingHit.Time = 1.f;
	bool bFilledHitResult = false;
	bool bIncludesOverlapsAtEnd = false;
	bool bRotationOnly = false;
	TArray<FOverlapInfo> PendingOverlaps;
	AActor* const Actor = GetOwner();

	{
		TArray<FHitResult> Hits;
		FVector NewLocation = TraceStart;

		// Perform movement collision checking if needed for this actor.
		const bool bCollisionEnabled = Prim->IsQueryCollisionEnabled();
		if (bCollisionEnabled && (DeltaSizeSq > 0.f))
		{
			UWorld* const MyWorld = GetWorld();

			FComponentQueryParams Params(SCENE_QUERY_STAT(MoveComponent), Actor);
			FCollisionResponseParams ResponseParam;
			Prim->InitSweepCollisionParams(Params, ResponseParam);
			//Params.bIgnoreTouches |= !(GetGenerateOverlapEvents());
			bool const bHadBlockingHit = MyWorld->ComponentSweepMulti(Hits, Prim, TraceStart, TraceEnd, InitialRotationQuat, Params);

			if (Hits.Num() > 0)
			{
				const float DeltaSize = FMath::Sqrt(DeltaSizeSq);
				for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
				{
					PullBackHit(Hits[HitIdx], TraceStart, TraceEnd, DeltaSize);
				}
			}

			// If we had a valid blocking hit, store it.
			// If we are looking for overlaps, store those as well.
			int32 FirstNonInitialOverlapIdx = INDEX_NONE;
			if (bHadBlockingHit)
			{
				int32 BlockingHitIndex = INDEX_NONE;
				float BlockingHitNormalDotDelta = BIG_NUMBER;
				for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
				{
					const FHitResult& TestHit = Hits[HitIdx];

					if (TestHit.bBlockingHit)
					{
						if (TestHit.Time == 0.f)
						{
							// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
							const float NormalDotDelta = (TestHit.ImpactNormal | velo);
							if (NormalDotDelta < BlockingHitNormalDotDelta)
							{
								BlockingHitNormalDotDelta = NormalDotDelta;
								BlockingHitIndex = HitIdx;
								//�������� Ŭ���� ������ �۾�����.
							}
						}
						else if (BlockingHitIndex == INDEX_NONE)
						{
							// First non-overlapping blocking hit should be used, if an overlapping hit was not.
							// This should be the only non-overlapping blocking hit, and last in the results.
							BlockingHitIndex = HitIdx;
							break;
						}
					}
				}

				// Update blocking hit, if there was a valid one.
				if (BlockingHitIndex >= 0)
				{
					BlockingHit = Hits[BlockingHitIndex];
					bFilledHitResult = true;
				}
			}

			// Update NewLocation based on the hit result
			if (!BlockingHit.bBlockingHit)
			{
				NewLocation = TraceEnd;
			}
			else
			{
				check(bFilledHitResult);
				NewLocation = TraceStart + (BlockingHit.Time * (TraceEnd - TraceStart));

				// Sanity check
				const FVector ToNewLocation = (NewLocation - TraceStart);
				if (ToNewLocation.SizeSquared() <= MinMovementDistSq)
				{
					// We don't want really small movements to put us on or inside a surface.
					NewLocation = TraceStart;
					BlockingHit.Time = 0.f;

					// Remove any pending overlaps after this point, we are not going as far as we swept.
					if (FirstNonInitialOverlapIdx != INDEX_NONE)
					{
						const bool bAllowShrinking = false;
						PendingOverlaps.SetNum(FirstNonInitialOverlapIdx, bAllowShrinking);
					}
				}
			}
		}
		else if (DeltaSizeSq > 0.f)
		{
			// apply move delta even if components has collisions disabled
			NewLocation += velo;
			bIncludesOverlapsAtEnd = false;
		}

		// Update the location.  This will teleport any child components as well (not sweep).
		//Prim->SetWorldLocationAndRotationNoPhysics(NewLocation, UpdatedComponent->GetComponentRotation());
	}


	// Handle blocking hit notifications. Avoid if pending kill (which could happen after overlaps).
	if (BlockingHit.bBlockingHit && !IsPendingKill())
	{
		check(bFilledHitResult);
		Prim->DispatchBlockingHit(*Actor, BlockingHit);
	}
	return false;
}

void UUSBMovementComponent::PullBackHit(FHitResult & Hit, const FVector & Start, const FVector & End, const float Dist)
{
	const float DesiredTimeBack = FMath::Clamp(0.1f, 0.1f / Dist, 1.f / Dist) + 0.001f;
	Hit.Time = FMath::Clamp(Hit.Time - DesiredTimeBack, 0.f, 1.f);
}