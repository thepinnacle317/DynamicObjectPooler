// Fill out your copyright notice in the Description page of Project Settings.


#include "ObjectPoolingComponent.h"
#include "PooledActorInterface.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UObjectPoolingComponent::UObjectPoolingComponent()
{

	PrimaryComponentTick.bCanEverTick = false;

	// Enable replication for the component
	SetIsReplicatedByDefault(true);
}

void UObjectPoolingComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UObjectPoolingComponent::InitializePool(TSubclassOf<AActor> ActorClass, int32 InitialSize)
{
	// Only initialize the object pool on the server
	if (!IsServer())
	{
		// TODO: Add On Screen Debug Message.
		return;
	}

	// Set the pooled object class to the assigned class
	PooledObjectClass = ActorClass;
	
	// Set the pool size assigned 
	PoolSize = InitialSize;

	// Expand the pool by the assigned size
	for (int32 i = 0; i < PoolSize; ++i)
	{
		ExpandPool();
	}
}

void UObjectPoolingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Replicate the pool array to keep it synchronized across clients
	DOREPLIFETIME(UObjectPoolingComponent, Pool);
}

void UObjectPoolingComponent::Multicast_OnPoolInitialized_Implementation()
{
	// Notify any listeners of the broadcast
	OnPoolInitialized.Broadcast(); 
	UE_LOG(LogTemp, Log, TEXT("Object pool has been initialized on clients."));
}

void UObjectPoolingComponent::HandleDestroyedActor(AActor* DestroyedActor)
{
	if (IsValid(DestroyedActor) && Pool.Contains(DestroyedActor))
	{
		ReturnObjectToPool(DestroyedActor);
	}
}

AActor* UObjectPoolingComponent::GetPooledObject()
{
	// Check if pool is initialized and has elements
	if (Pool.Num() == 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(1, 5.f, FColor::Emerald,
				TEXT("Pool is empty! Did you call InitializePool?"));
		}
		
		UE_LOG(LogTemp, Warning, TEXT("Pool is empty! Did you call InitializePool?"));
		return nullptr;
	}
	
	for (AActor* Actor : Pool)
	{
		if (IsValid(Actor) && Actor->IsHidden())
		{
			// Make the actor visible before returning it
			Actor->SetActorHiddenInGame(false);
			Actor->SetActorEnableCollision(true);
			Actor->SetActorTickEnabled(true);
			Actor->SetReplicates(true);
			Actor->SetReplicateMovement(true);
			return Actor;
		}
	}

	// If no inactive objects are available, consider expanding the pool if needed
	if (bAutoExpand)  // Assume bAutoExpand is a boolean we defined earlier
	{
		UE_LOG(LogTemp, Warning, TEXT("Expanding pool as no inactive objects are available."));
		ExpandPool();
        
		// Return an inactive actor from the pool that was added
		return Pool.Last(); 
	}

	
	// If auto-expansion is disabled and no object is available, return null
	UE_LOG(LogTemp, Warning, TEXT("No available pooled objects, and pool auto-expansion is disabled."));
	
	return nullptr; 
}

void UObjectPoolingComponent::ReturnObjectToPool(AActor* Actor)
{
	// Check that it is being called from the server, has been passed a valid actor, and contains a valid actor.
	if (!GetOwner()->HasAuthority() || !Actor || !Pool.Contains(Actor)) return;

	UE_LOG(LogTemp, Log, TEXT("Returning actor to pool: %s"), *Actor->GetName());
	
	// Handle Actor Properties
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);
	Actor->SetActorLocation(FVector::ZeroVector);

	// Stop replicating movement when the actor is returned to the pool
	Actor->SetReplicates(false);
	Actor->SetReplicateMovement(false);

	// Check if using the lifespan timer
	if (bUseTimerLifespan)
	{
		GetWorld()->GetTimerManager().ClearAllTimersForObject(Actor);
	}

	// Decrement the active objects and set the amount of inactive objects
	ActiveObjects--;
	InactiveObjects = Pool.Num() - ActiveObjects;
	TotalReturnRequests++;
	
	// Broadcast event when the actor is returned to the pool
	OnPooledActorReturned.Broadcast(Actor);	
}

AActor* UObjectPoolingComponent::SpawnPooledActor(const FTransform& SpawnTransform)
{
	if (!IsServer()) return nullptr; // Only the server should spawn objects
	
	TotalSpawnRequests++;

	AActor* PooledActor = GetPooledObject();
	if (PooledActor)
	{
		// Check if the actor implements UPooledActorInterface
		// Reset the actor before being reused again
		if (PooledActor->GetClass()->ImplementsInterface(UPooledActorInterface::StaticClass()))
		{
			IPooledActorInterface::Execute_ResetPooledActor(PooledActor);
		}
		
		// Set location and rotation
		PooledActor->SetActorTransform(SpawnTransform);

		// Make the actor visible and enable interaction
		PooledActor->SetActorHiddenInGame(false);
		PooledActor->SetActorEnableCollision(true);
		PooledActor->SetActorTickEnabled(true);

		if (!bUseTimerLifespan)
		{
			PooledActor->OnDestroyed.AddDynamic(this, &UObjectPoolingComponent::HandleDestroyedActor);
		}

		// Notify clients about the activation
		PooledActor->SetReplicates(true);
		PooledActor->SetReplicateMovement(true);

		TotalObjectsCreated++;
		ActiveObjects++;
		InactiveObjects = Pool.Num() - ActiveObjects;

		// Update peak usage if active objects exceed previous peak
		if (ActiveObjects > PeakUsage)
		{
			PeakUsage = ActiveObjects;
		}

		// Will use the timer based on the actor lifespan to return the actor back to the pool.
		if (bUseTimerLifespan)
		{
			GetWorld()->GetTimerManager().SetTimerForNextTick([this, PooledActor]()
			{
				/* Timer handle used to manage the lifespan of the actor. */
				FTimerHandle LifespanTimerHandle;
				GetWorld()->GetTimerManager().SetTimer(LifespanTimerHandle, FTimerDelegate::CreateUObject(this, &UObjectPoolingComponent::ReturnObjectToPool, PooledActor),
					ActorLifespan,
					false
					);
			}
			);
		}
		else
		{
			// This will cause the actor to destroy itself and then force the system to create a new spot in memory at the end of the array.
			// This works great if you do not want to manually reset data for reusing the actor.
			PooledActor->SetLifeSpan(ActorLifespan);
		}
		
		// Broadcast event when pooled actor is spawned
		OnPooledActorSpawned.Broadcast(PooledActor);
		
		return PooledActor;
	}

	// Return null if no actor is available
	return nullptr; 
}

void UObjectPoolingComponent::ExpandPool()
{
	// Check that there if a valid context object and valid class assigned to be pooled
	if (!IsServer() || !GetWorld() || !PooledObjectClass) return;

	AActor* NewActor = GetWorld()->SpawnActor<AActor>(PooledObjectClass);
	if (IsValid(NewActor))
	{
		// Handle Actor Properties
		NewActor->SetActorHiddenInGame(true);
		NewActor->SetActorEnableCollision(false);
		NewActor->SetActorTickEnabled(false);

		// Set replication on the new actor
		NewActor->SetReplicates(true);
		NewActor->SetReplicateMovement(false);

		// Add the actor to the pool
		Pool.Add(NewActor);

		// Recalculate the inactive objects and increment objects created and amount of expansions
		TotalObjectsCreated++;
		TotalPoolExpansions++;
		InactiveObjects = Pool.Num() - ActiveObjects;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to spawn actor for the pool."));
	}
}

