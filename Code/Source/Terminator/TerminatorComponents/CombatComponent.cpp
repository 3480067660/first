#include "CombatComponent.h"
#include "Terminator/Weapon/Weapon.h"
#include "Terminator/Character/TerminatorCharacter.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "Terminator/PlayerController/TerminatorPlayerController.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "Sound/SoundCue.h"


UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	BaseWalkSpeed = 600.f;
	MainWeaponWalkSpeed = 520.f;
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCombatComponent, EquippedWeapon);
	DOREPLIFETIME(UCombatComponent, CombatState);
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;

		if (Character->GetCamera())
		{
			DefaultFOV = Character->GetCamera()->FieldOfView;
			CurrentFOV = DefaultFOV;
		}
	}
	
}

void UCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Character && Character->IsLocallyControlled())
	{
		FHitResult HitResult;
		TraceUnderCrosshairs(HitResult);
		HitTarget = HitResult.ImpactPoint;

		SetHUDCrosshairs(DeltaTime);
	}
}

void UCombatComponent::FireButtonPressed(bool bPressed)
{
	bFiredButtonPressed = bPressed;

	if (bFiredButtonPressed && EquippedWeapon)
	{
		Fire();
	}
}

// Server and Client
void UCombatComponent::Fire()
{
	if (CanFire())
	{
		// �ܶ�ʱ���ڽ�ֹ���� StartFireTimer�����õ�TimerHandle������������bCanFireΪtrue
		bCanFire = false;
		ServerFire(HitTarget);
		if (EquippedWeapon)
		{
			CrosshairShootFactor = 1.3f;
			bCanReduceCrosshairShootFactor = false;
		}
		StartFireTimer();
	}
}

void UCombatComponent::StartFireTimer()
{
	if (Character == nullptr || EquippedWeapon == nullptr) { return; }
	Character->GetWorldTimerManager().SetTimer(FireTimer, this, &UCombatComponent::FireTimerFinished, EquippedWeapon->FireDelay);
}

void UCombatComponent::FireTimerFinished()
{
	if (EquippedWeapon == nullptr) { return; }
	bCanFire = true;
	bCanReduceCrosshairShootFactor = true;
	if (bFiredButtonPressed && EquippedWeapon->bAutomatic)
	{
		Fire();
	}
	if (EquippedWeapon->IsEmpty())
	{
		Reload();
	}
}

// Mutlticast: happen on all machines
void UCombatComponent::MulticastFire_Implementation(const FVector_NetQuantize& TraceHitTarget)
{
	if (EquippedWeapon == nullptr) { return; }
	if (Character && CombatState == ECombatState::ECS_Unoccupied)
	{
		Character->PlayFireMontage();
		Character->PlayFPSFireMontage();
		EquippedWeapon->Fire(TraceHitTarget);
	}
}

// Server Only (Reliable RPC)
void UCombatComponent::ServerFire_Implementation(const FVector_NetQuantize& TraceHitTarget)
{
	MulticastFire(TraceHitTarget);
}

// Server Only
void UCombatComponent::EquipWeapon(AWeapon* WeaponToEquip)
{
	if (Character == nullptr || WeaponToEquip == nullptr) { return; }

	EquippedWeapon = WeaponToEquip;
	EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);			// WeaponState is replicated.

	const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(FName("RightHandSocket"));
	if (HandSocket && !Character->IsLocallyControlled())
	{
		HandSocket->AttachActor(EquippedWeapon, Character->GetMesh());			// Propagate in RepNotify OnRep_EquippedWeapon
	}
	const USkeletalMeshSocket* FPSHandSocket = Character->GetFPSMesh()->GetSocketByName(FName("RightHandSocket"));
	if (FPSHandSocket && Character->IsLocallyControlled())
	{
		FPSHandSocket->AttachActor(EquippedWeapon, Character->GetFPSMesh());	// Propagate in RepNotify OnRep_EquippedWeapon
	}

	EquippedWeapon->SetOwner(Character);                                // Propagate
	EquippedWeapon->SetHUDAmmo();                                       // Client call this function in OnRep_Owner in weapon.cpp
	EquippedWeapon->SetHUDCarriedAmmo();                                // Client call this function in OnRep_Owner in weapon.cpp

	if (EquippedWeapon->EquipSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EquippedWeapon->EquipSound, Character->GetActorLocation());
	}

	if (Character)
	{
		Character->GetCharacterMovement()->MaxWalkSpeed = MainWeaponWalkSpeed;
	}
}

void UCombatComponent::Reload()
{
	if (EquippedWeapon->GetCarriedAmmo() > 0 && !EquippedWeapon->IsFull() && CombatState != ECombatState::ECS_Reloading)
	{
		ServerReload();
	}
}

void UCombatComponent::ServerReload_Implementation()
{
	if (Character == nullptr || EquippedWeapon == nullptr) { return; }

	CombatState = ECombatState::ECS_Reloading;
	HandleReload();
}

void UCombatComponent::FinishReloading()
{
	if (Character == nullptr || EquippedWeapon == nullptr) { return; }
	if (Character->HasAuthority())
	{
		CombatState = ECombatState::ECS_Unoccupied;
		int32 ReloadAmount = AmountToReload();
		EquippedWeapon->AddAmmo(ReloadAmount);
		EquippedWeapon->MinusCarriedAmmo(ReloadAmount);
	}
	if (bFiredButtonPressed)
	{
		Fire();
	}
}

void UCombatComponent::FillAmmo()
{
	if (EquippedWeapon)
	{
		EquippedWeapon->FillAmmoAndCarriedAmmo();
	}
}

void UCombatComponent::OnRep_CombatState()
{
	switch (CombatState)
	{
	case ECombatState::ECS_Reloading:
		HandleReload();
		break;
	case ECombatState::ECS_Unoccupied:
		if (bFiredButtonPressed)
		{
			Fire();
		}
		break;
	}
}

void UCombatComponent::HandleReload()
{
	Character->PlayReloadMontage();		// Consider both tps and fps
	if (Character->IsLocallyControlled())
	{
		EquippedWeapon->PlayReloadAnimation();
	}
}

int32 UCombatComponent::AmountToReload()
{
	if (EquippedWeapon == nullptr) { return 0; }
	int32 RoomInMag = EquippedWeapon->GetMagCapacity() - EquippedWeapon->GetAmmo();
	int32 CarriedAmmo = EquippedWeapon->GetCarriedAmmo();
	return FMath::Clamp(RoomInMag, 0, FMath::Min(RoomInMag, CarriedAmmo));
	// return FMath::Min(RoomInMag, CarriedAmmo);		also right
}

void UCombatComponent::OnRep_EquippedWeapon()
{
	if (EquippedWeapon && Character)
	{
		EquippedWeapon->SetWeaponState(EWeaponState::EWS_Equipped);	
		const USkeletalMeshSocket* HandSocket = Character->GetMesh()->GetSocketByName(FName("RightHandSocket"));
		if (HandSocket && !Character->IsLocallyControlled())
		{
			HandSocket->AttachActor(EquippedWeapon, Character->GetMesh());
		}
		const USkeletalMeshSocket* FPSHandSocket = Character->GetFPSMesh()->GetSocketByName(FName("RightHandSocket"));
		if (FPSHandSocket && Character->IsLocallyControlled())
		{
			FPSHandSocket->AttachActor(EquippedWeapon, Character->GetFPSMesh());
		}
	}

	if (EquippedWeapon->EquipSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, EquippedWeapon->EquipSound, Character->GetActorLocation());
	}
}

void UCombatComponent::TraceUnderCrosshairs(FHitResult& TraceHitResult)
{
	FVector2D ViewportSize;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}

	FVector2D CrosshairLocation(ViewportSize.X / 2.f, ViewportSize.Y / 2.f);
	FVector CrosshairWorldPosition;
	FVector CrosshairWorldDirection;
	bool bScreenToWorld = UGameplayStatics::DeprojectScreenToWorld(UGameplayStatics::GetPlayerController(this, 0), CrosshairLocation,
		CrosshairWorldPosition, CrosshairWorldDirection);
	if (bScreenToWorld)
	{
		FVector Start = CrosshairWorldPosition;

		if (Character)
		{
			float DistanceToCharacter = (Character->GetActorLocation() - Start).Size();
			Start += CrosshairWorldDirection * (DistanceToCharacter + 100.f);
			//DrawDebugSphere(GetWorld(), Start, 16.f, 12.f, FColor::Red, false);
		}

		FVector End = Start + CrosshairWorldDirection * TRACE_LENGTH;

		GetWorld()->LineTraceSingleByChannel(TraceHitResult, Start, End, ECollisionChannel::ECC_Visibility);

		//DrawDebugSphere(GetWorld(), TraceHitResult.ImpactPoint, 12.f, 12, FColor::Red);
	}
}

void UCombatComponent::SetHUDCrosshairs(float DeltaTime)
{
	if (Character == nullptr || Character->Controller == nullptr) { return; }

	Controller = Controller == nullptr ? Cast<ATerminatorPlayerController>(Character->Controller) : Controller;
	if (Controller)
	{
		HUD = HUD == nullptr ? Cast<ATerminatorHUD>(Controller->GetHUD()) : HUD;
		if (HUD)
		{
			if (EquippedWeapon)
			{
				HUDPackage.CrosshairsCenter = EquippedWeapon->CrosshairsCenter;
				HUDPackage.CrosshairsLeft = EquippedWeapon->CrosshairsLeft;
				HUDPackage.CrosshairsRight = EquippedWeapon->CrosshairsRight;
				HUDPackage.CrosshairsBottom = EquippedWeapon->CrosshairsBottom;
				HUDPackage.CrosshairsTop = EquippedWeapon->CrosshairsTop;
				HUDPackage.CrosshairsCircle = EquippedWeapon->CrosshairsCircle;
			}
			else
			{
				HUDPackage.CrosshairsCenter = nullptr;
				HUDPackage.CrosshairsLeft = nullptr;
				HUDPackage.CrosshairsRight = nullptr;
				HUDPackage.CrosshairsBottom = nullptr;
				HUDPackage.CrosshairsTop = nullptr;
			}

			// Calculate crosshair spread range from [0,1]
			FVector2D WaldSpeedRange(0.f, Character->GetCharacterMovement()->MaxWalkSpeed);
			FVector2D CrosshairSpreadRange(0.f, 1.f);
			FVector Velocity = Character->GetVelocity();
			Velocity.Z = 0.f;

			CrosshairVelocityFactor = FMath::GetMappedRangeValueClamped(WaldSpeedRange, CrosshairSpreadRange, Velocity.Size());

			if (Character->GetCharacterMovement()->IsFalling())
			{
				CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 1.f, DeltaTime, 10.f);
			}
			else
			{
				CrosshairInAirFactor = FMath::FInterpTo(CrosshairInAirFactor, 0.f, DeltaTime, 10.f);
			}

			if (bCanReduceCrosshairShootFactor)
			{
				// ��ҿ�����һ��ᣬ��ֵ��ʼ��С
				CrosshairShootFactor = FMath::FInterpTo(CrosshairShootFactor, 0.f, DeltaTime, 5.f);
			}

			// ��ɫ�ٶ� �Ƿ��ڿ��� �Ƿ�ǹ ��ͬ����CrosshairSpread
			HUDPackage.CrosshairSpread = CrosshairVelocityFactor + CrosshairInAirFactor + CrosshairShootFactor;

			// ���з��������� �����ͨ׼��
			if (HitCrosshairScale <= 0.95f)
			{
				HUDPackage.bHit = false;
			}
			else
			{
				// �������ʾ���з��� ���з�����֡��С
				HitCrosshairScale = FMath::FInterpTo(HitCrosshairScale, 0.8f, DeltaTime, 10.f);
				HUDPackage.bHit = true;
				HUDPackage.HitScale = HitCrosshairScale;
			}

			HUD->SetHUDPackage(HUDPackage);
		}
	}
}

bool UCombatComponent::CanFire()
{
	if (EquippedWeapon == nullptr) { return false; }
	if (EquippedWeapon->IsEmpty()) { return false; }
	return bCanFire && CombatState == ECombatState::ECS_Unoccupied;
}

