#include "Cube.h"
#include "Terminator/GameMode/TerminatorGameMode.h"
#include "Terminator/PlayerController/TerminatorPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundCue.h"
#include "Net/UnrealNetwork.h"
#include "Terminator/Character/TerminatorCharacter.h"

ACube::ACube()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	CubeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CubeMesh"));
	SetRootComponent(CubeMesh);
	CubeMesh->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);
	CubeMesh->SetCollisionResponseToChannel(ECollisionChannel::ECC_Pawn, ECollisionResponse::ECR_Block);
}

void ACube::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DOREPLIFETIME(ACube, bDoubleScore);
	DOREPLIFETIME(ACube, Scale);
}

void ACube::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// ֻ��Server�����˺�
		OnTakeAnyDamage.AddDynamic(this, &ACube::ReceiveDamage);
	}
}
// Server Only
void ACube::ReceiveDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatorController, AActor* DamageCauser)
{
	ATerminatorGameMode* TerminatorGameMode = GetWorld()->GetAuthGameMode<ATerminatorGameMode>();
	if (TerminatorGameMode == nullptr) return;
	
	Health -= Damage;
	// ����Ѫ��Ϊ0
	if (!Health)
	{
		ATerminatorPlayerController* AttackController = Cast<ATerminatorPlayerController>(InstigatorController);
		// �ú���ΪAttacker�ӷֲ�����HUD
		TerminatorGameMode->CubeEliminated(this, AttackController);

		// �������ҪĿ��ķ��飬����Attacker��Client�ϲ��Ż�ɱ��Ч
		if (bDoubleScore)
		{
			AttackController->PlayLocallyDoubleScoreSound(DoubleScoreSound);
		}

		Destroy();
	}
	// ����Ѫ������Ϊ0
	else
	{
		// Scale��Replicated��������Client��Ҳ��SetScale����������С��Ч��
		Scale *= 0.5f;
		SetScale(Scale);
		if (ScaleSound)
		{
			UGameplayStatics::PlaySoundAtLocation(this, ScaleSound, GetActorLocation());
		}
	}
}

void ACube::OnRep_Scale()
{
	SetScale(Scale);
	if (ScaleSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ScaleSound, GetActorLocation());
	}
}

void ACube::SetScale(float CubeScale)
{
	if (CubeMesh)
	{
		CubeMesh->SetWorldScale3D(FVector(CubeScale, CubeScale, CubeScale));
	}
}

void ACube::OnRep_DoubleScore()
{
	if (DoubleScoreCubeMesh)
	{
		CubeMesh->SetStaticMesh(DoubleScoreCubeMesh);
	}
}

void ACube::SetDoubleScoreCube()
{
	bDoubleScore = true;
	Health = 2.f;
	if (DoubleScoreCubeMesh)
	{
		CubeMesh->SetStaticMesh(DoubleScoreCubeMesh);
	}
}

void ACube::Destroyed()
{
	if (DestroySound)
	{
		UGameplayStatics::PlaySound2D(this, DestroySound);
	}

	Super::Destroyed();
}



