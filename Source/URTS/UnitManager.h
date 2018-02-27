// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "UnitManager.generated.h"

UCLASS()
class URTS_API AUnitManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AUnitManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY() UInstancedStaticMeshComponent* InstancedStaticMeshComponent;
	UPROPERTY(EditAnywhere)	UStaticMesh* SMAsset_Cube;
	
private:
	FRotator DEBUGGetRandomRotation();
	FVector DEBUGGetRandomPositionInBox(FVector Dimensions);
	FTransform DEBUGGetRandomTransform(FVector Dimensions);

	UPROPERTY(EditAnywhere) int UnitLimit = 1000000;

	// Spawn area
	FVector Dimensions = FVector(10000.0f, 10000.0f, 1000.0f);

	/// TODO(Thomas): Make this just an index
	// Lazy counter, doesnt even need to be thread safe.
	FThreadSafeCounter ThreadID;
};