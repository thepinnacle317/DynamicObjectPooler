// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "ObjectPoolingComponent.generated.h"

// Used for when an actor is spawned from the pool
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPooledActorSpawned, AActor*, Actor);

// Used for when an actor is returned to the pool
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPooledActorReturned, AActor*, Actor);

// Delegate for notifying when the pool is initialized
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPoolInitialized);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class DYNAMICOBJECTPOOLER_API UObjectPoolingComponent : public UActorComponent
{
	GENERATED_BODY()

public:	

	UObjectPoolingComponent();

	/* Gets an inactive/Hidden pooled object if there is one available.  If there are none then the pool is expanded */
	UFUNCTION(BlueprintCallable, Category="Dynamic Object Pooling")
	AActor* GetPooledObject();

	/* Returns and object back into the pool and handles the actors properties: hide, disable collision, set inactive and no replication */
	UFUNCTION(BlueprintCallable, Category="Dynamic Object Pooling")
	void ReturnObjectToPool(AActor* Actor);

	/* Initialized the pool with the selected actor class and handle initial replication settings and actor properties */
	UFUNCTION(BlueprintCallable, Category="Dynamic Object Pooling")
	void InitializePool(TSubclassOf<AActor> ActorClass, int32 InitialSize);

	/* Used to spawn pooled actors over the engine method */
	UFUNCTION(BlueprintCallable, Category = "Dynamic Object Pooling")
	AActor* SpawnPooledActor(const FTransform& SpawnTransform);

	// Get the current number of pooled objects
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Pooling")
	int32 GetTotalObjectsCreated() const { return TotalObjectsCreated; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dynamic Object Pooling")
	bool bAutoExpand = false;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic Object Pooling | Delegates")
	FOnPooledActorSpawned OnPooledActorSpawned;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic Object Pooling | Delegates")
	FOnPooledActorReturned OnPooledActorReturned;

	UPROPERTY(BlueprintAssignable, Category = "Dynamic Object Pooling | Delegates")
	FOnPoolInitialized OnPoolInitialized;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dynamic Object Pooling")
	float ActorLifespan;

	
	/* Pooling Statistics Variables */

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 TotalObjectsCreated = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 ActiveObjects = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 InactiveObjects = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 TotalSpawnRequests = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 TotalReturnRequests = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 TotalPoolExpansions = 0;

	/* */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Pooling Statistics")
	int32 PeakUsage = 0;

protected:

	virtual void BeginPlay() override;
	
	// RPC to notify clients when the pool is ready
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_OnPoolInitialized();
	
	// Initial size of the pool for Async
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dynamic Object Pooling | Async", meta = (ClampMin = "1", UIMin = "1"))
	int32 InitialPoolSize = 10;

private:
	/* The class that will be assigned to the pool */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dynamic Object Pooling", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<AActor> PooledObjectClass;

	/* The class that will be assigned to the pool for Async Loading */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dynamic Object Pooling", meta = (AllowPrivateAccess = "true"))
	TSoftClassPtr<AActor> SoftPooledObjectClass;

	/* The array of actors that will be used to pull from and return to */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category="Dynamic Object Pooling", meta = (AllowPrivateAccess = "true"))
	TArray<AActor*> Pool;

	/* How large the pool you wish to set aside in memory */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dynamic Object Pooling", meta = (AllowPrivateAccess = "true"))
	int32 PoolSize = 0;

	// Server-only management of the pool
	bool IsServer() const { return GetOwner()->HasAuthority(); }

	/* Used to expand the object pool ** Called in intialize and GetPooledObject */
	void ExpandPool();

	UPROPERTY()
	FTransform InitialSpawnTransform;

	UFUNCTION()
	void HandleDestroyedActor(AActor* DestroyedActor);
	
};
