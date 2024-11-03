// Fill out your copyright notice in the Description page of Project Settings.


#include "ObjectPoolingComponent.h"
#include "PooledActorInterface.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
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

void UObjectPoolingComponent::InitializePoolAsync_Implementation(int32 InitialSize)
{
	if (!IsServer())
	{
		// Ensure only the server initiates this function
		return;
	}
	
	InitialSpawnTransform = FTransform(FRotator(0, 0, 0), FVector(0, 0, 100));
	
	if (!IsValid(PooledObjectClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("PooledObjectClass is not set."));
		return;
	}

	if (IsValid(PooledObjectClass))
	{
		// Already loaded, so expand the pool immediately
		ExpandPoolAsync(InitialSpawnTransform);
	}
	else
	{
		// Load the pooled actor class asynchronously if it is not loaded yet
		FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
		StreamableManager.RequestAsyncLoad(
			SoftPooledObjectClass.ToSoftObjectPath(),
			FStreamableDelegate::CreateLambda([this]()
			{
				ExpandPoolAsync(InitialSpawnTransform);			
			})
		);
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
	
	// Handle Actor Properties
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorEnableCollision(false);
	Actor->SetActorTickEnabled(false);
	Actor->SetActorLocation(FVector::ZeroVector);

	// Stop replicating movement when the actor is returned to the pool
	Actor->SetReplicates(false);
	Actor->SetReplicateMovement(false);

	// Decrement the active objects and set the amount of inactive objects
	ActiveObjects--;
	InactiveObjects = Pool.Num() - ActiveObjects;

	// Broadcast event when the actor is returned to the pool
	OnPooledActorReturned.Broadcast(Actor);

	TotalReturnRequests++;
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

		PooledActor->SetLifeSpan(ActorLifespan);
		PooledActor->OnDestroyed.AddDynamic(this, &UObjectPoolingComponent::HandleDestroyedActor);

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

		// Broadcast event when pooled actor is spawned
		OnPooledActorSpawned.Broadcast(PooledActor);
		
		return PooledActor;
	}

	// Return null if no actor is available
	return nullptr; 
}

void UObjectPoolingComponent::SpawnPooledActorAsync(UClass* ActorClass, const FTransform& SpawnTransform)
{
	if (!ActorClass) return;  // Check if the class is valid

	UWorld* World = GetWorld();  // Get the world context beforehand
	if (!IsValid(World)) return;  // Ensure the world context is valid

	// Start an asynchronous task
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ActorClass, SpawnTransform, World]()
	{
		// Instead of spawning here, just pass the necessary information to the main thread
		AsyncTask(ENamedThreads::GameThread, [this, ActorClass, SpawnTransform, World]()
		{
			if (!IsValid(World)) return;  // Ensure the world context is still valid

			// Spawn the actor in the game world, now on the game thread
			AActor* NewActor = World->SpawnActor<AActor>(ActorClass, SpawnTransform);
			if (IsValid(NewActor))
			{
				// Configure properties before adding to the pool
				NewActor->SetActorHiddenInGame(true);
				NewActor->SetActorEnableCollision(false);
				NewActor->SetActorTickEnabled(false);
				NewActor->SetReplicates(true);
				NewActor->SetReplicateMovement(true);

				// Add the newly spawned actor to the pool
				Pool.Add(NewActor);
				TotalObjectsCreated++;

				UE_LOG(LogTemp, Log, TEXT("Async pooled actor spawned and added to pool."));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to asynchronously spawn actor for the pool."));
			}
		});
	});
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

void UObjectPoolingComponent::ExpandPoolAsync(const FTransform& SpawnTransform)
{
	// Ensure only the server expands the pool
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority()) return;

	// Load the actor class if it's not already loaded
	if (!IsValid(PooledObjectClass))
	{
		if (SoftPooledObjectClass.IsValid())
		{
			// Asynchronously load the asset
			FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
			StreamableManager.RequestAsyncLoad(
				SoftPooledObjectClass.ToSoftObjectPath(),
				FStreamableDelegate::CreateUObject(this, &UObjectPoolingComponent::OnActorClassLoaded)
			);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("SoftPooledObjectClass is not valid."));
		}
	}
	else
	{
		// Actor class already loaded, so begin spawning actors
		StartAsyncSpawning();
	}
}

void UObjectPoolingComponent::StartAsyncSpawning()
{
	// Expand the pool by spawning actors
	for (int32 i = 0; i < InitialPoolSize; i++)
	{
		// Start an asynchronous spawn operation for each actor
		SpawnPooledActorAsync(PooledObjectClass, InitialSpawnTransform);
	}

	// Notify all clients that the pool has been initialized
	Multicast_OnPoolInitialized();
}

void UObjectPoolingComponent::OnActorClassLoaded()
{
	// Assign the loaded class to PooledObjectClass
	PooledObjectClass = SoftPooledObjectClass.Get();
	if (!PooledObjectClass)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load pooled actor class."));
		return;
	}

	// Now that the class is loaded, proceed to expand the pool
	ExpandPoolAsync(InitialSpawnTransform);
}

bool UObjectPoolingComponent::InitializePoolAsync_Validate(int32 InitialSize)
{
	// Can add any validation logic that is required here.
	return true;
}
