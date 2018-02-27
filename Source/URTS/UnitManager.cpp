// Fill out your copyright notice in the Description page of Project Settings.

#include "UnitManager.h"
#include "Runnable.h"
#include "RunnableThread.h"

struct WorkOrder
{
	UInstancedStaticMeshComponent* Instances;
	uint32 MinIndex;
	uint32 MaxIndex;
};

struct WorkQueue
{
	WorkOrder *WorkOrders;
	uint32 WorkOrderCount;

	FThreadSafeCounter NextWorkOrderIndex;
};

static bool UpdateTransforms(WorkQueue *Queue);

class FUpdateTransformsWorker : public FRunnable
{
public:
	WorkQueue *Queue;

	uint32 Run()
	{
		while(UpdateTransforms(Queue))
		{}

		return 0;
	}
};

// Sets default values
AUnitManager::AUnitManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	//PrimaryActorTick.TickInterval = 0.5f;

	/// TODO(Thomas): Set as root component? there's some warning floating around in the editor
	// create new object
	InstancedStaticMeshComponent = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HierarchicalInstancedStaticMesh"));
	InstancedStaticMeshComponent->SetFlags(RF_Transactional); /// TODO(Thomas): Figure out what this tag is.
	
	//InstancedStaticMeshComponent->bSelectable = false; // To be able to select in editor or not. Should only affect editor performance.
	
	/// TODO(Thomas): Does order matter?
	/// Disable collision functionality of unreal engine.
	InstancedStaticMeshComponent->bHasPerInstanceHitProxies = false;
	static FName NoCollision(TEXT("NoCollision"));
	InstancedStaticMeshComponent->SetCollisionProfileName(NoCollision);
	InstancedStaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InstancedStaticMeshComponent->SetCanEverAffectNavigation(false);
	InstancedStaticMeshComponent->bAlwaysCreatePhysicsState = false;
	InstancedStaticMeshComponent->SetSimulatePhysics(false);
	InstancedStaticMeshComponent->bGenerateOverlapEvents = false;
	

	InstancedStaticMeshComponent->RegisterComponent();
	InstancedStaticMeshComponent->DestroyPhysicsState();
	AddInstanceComponent(InstancedStaticMeshComponent);
}

// Called when the game starts or when spawned
void AUnitManager::BeginPlay()
{
	Super::BeginPlay();
	
	InstancedStaticMeshComponent->SetStaticMesh(SMAsset_Cube);

	DEBUGGetRandomTransform(FVector(100.0f));

	int SpawnAmount = FMath::Min(50000, UnitLimit);

	for (int i = 0; i < SpawnAmount; i++)
	{
		InstancedStaticMeshComponent->AddInstance(DEBUGGetRandomTransform(Dimensions));
	}

	int InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();
	UE_LOG(LogTemp, Warning, TEXT("%d"), InstanceCount);
}

// Called every frame
void AUnitManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	int32 InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();

	// correct spawn amount first
	if (InstanceCount < UnitLimit)
	{
		for (int i = 0; i < 50000; i++)
		{
			InstancedStaticMeshComponent->AddInstance(DEBUGGetRandomTransform(Dimensions));
		}

		int InstanceCount = InstancedStaticMeshComponent->GetInstanceCount();
		UE_LOG(LogTemp, Warning, TEXT("%d"), InstanceCount);

		return; // not executing more until we've met the spawn count
	}

	int32 CoreCount = 4;

	// Compute work size
	int32 BlockCount = CoreCount;
	int32 BlockSize = FMath::DivideAndRoundDown(InstanceCount, BlockCount);
	
	WorkQueue Queue = {};
	Queue.WorkOrders = (WorkOrder *)FMemory::Malloc(BlockCount * sizeof(WorkOrder)); // Might want to subdivide further.

	int BlockStart = 0;

	for (int i = 0; i < BlockCount; i++)
	{
		WorkOrder *Order = Queue.WorkOrders + Queue.WorkOrderCount++;
		
		Order->Instances = InstancedStaticMeshComponent;
		Order->MinIndex = BlockStart;
		Order->MaxIndex = BlockStart + BlockSize;

		BlockStart += BlockSize;
	}

	// Correction to final index in case of rounding
	Queue.WorkOrders[BlockCount - 1].MaxIndex = InstanceCount;

	// The place for us to save our thread handles
	FRunnableThread **Threads = 0;
	if (CoreCount > 1)
	{
		Threads = (FRunnableThread **)FMemory::Malloc((CoreCount - 1) * sizeof(FRunnableThread));
	}

	// Skip first index because this thread also runs computations
	for (int i = 1; i < CoreCount; i++)
	{
		FUpdateTransformsWorker Worker = {};
		Worker.Queue = &Queue;

		/// TODO(Thomas): Maybe create a thread pool to reuse.
		// Namingscheme just to get rid of a warning.
		FString ThreadName(FString::Printf(TEXT("UpdatePositionWorker%i"), ThreadID.Increment()));
		Threads[i-1] = FRunnableThread::Create(&Worker, *ThreadName, 0, EThreadPriority::TPri_AboveNormal);
	}

	while(UpdateTransforms(&Queue))
	{}

	// Cycle through all threads to make sure they are done.
	for (int i = 1; i < CoreCount; i++)
	{
		Threads[i - 1]->WaitForCompletion();
	}
	
	// Mark render state
	InstancedStaticMeshComponent->MarkRenderStateDirty();

	FMemory::Free(Threads);
	FMemory::Free(Queue.WorkOrders);
}

// ---------------------------------
// Debug: spawning related functions
// ---------------------------------

FRotator AUnitManager::DEBUGGetRandomRotation()
{
	float pitch = FMath::FRandRange(-180.0f, 180.0f);
	float yaw = FMath::FRandRange(-180.0f, 180.0f);
	float roll = FMath::FRandRange(-180.0f, 180.0f);
	return FRotator(pitch, yaw, roll);
}

FVector AUnitManager::DEBUGGetRandomPositionInBox(FVector Dimensions)
{
	float X = FMath::FRand();
	float Y = FMath::FRand();
	float Z = FMath::FRand();

	float scaledX = X * Dimensions.X - (Dimensions.X / 2.0f);
	float scaledY = Y * Dimensions.Y - (Dimensions.Y / 2.0f);
	float scaledZ = Z * Dimensions.Z - (Dimensions.Z / 2.0f);

	return FVector(scaledX, scaledY, scaledZ);
}

FTransform AUnitManager::DEBUGGetRandomTransform(FVector Dimensions)
{
	return FTransform(DEBUGGetRandomRotation(), DEBUGGetRandomPositionInBox(Dimensions), FVector(1.0f));
}

// Update the position data of all the instances
static bool UpdateTransforms(WorkQueue *Queue)
{
	/// NOTE: DO NOT REPLACE WITH INCREMENT
	uint32 WorkOrderIndex = Queue->NextWorkOrderIndex.Add(1);
	if (WorkOrderIndex >= Queue->WorkOrderCount)
	{
		return false;
	}

	WorkOrder *Order = Queue->WorkOrders + WorkOrderIndex;

	// Arbitrarily chosen local transformation to aply.
	FTransform movement = FTransform(FRotator(1.0f, 0.0f, 0.0f));
	FMatrix movMatrix = movement.ToMatrixNoScale();

	for (uint32 i = Order->MinIndex; i < Order->MaxIndex; i++)
	{
		FMatrix position = Order->Instances->PerInstanceSMData[i].Transform;
		FMatrix newPosition = movMatrix * position;
		FTransform newPositionTransform = FTransform(newPosition);

		/// TODO(Thomas): Bypass this call and set the transform ourself? 
		/// UE code seems to be overly conservative. So at certain points we can shave off time?
		/// But having issues marking the render state dirty so the change actually shows.
		Order->Instances->UpdateInstanceTransform(i, newPositionTransform, false, false, true);

		//InstancedStaticMeshComponent->PerInstanceSMData[i].Transform = newPosition;
		//InstancedStaticMeshComponent->MarkRenderStateDirty();
	}

	return true;
}

