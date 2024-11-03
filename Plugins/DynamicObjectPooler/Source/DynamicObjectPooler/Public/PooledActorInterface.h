// Nicholas Bonofiglio @ Pinnacle Gaming Studios

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PooledActorInterface.generated.h"


UINTERFACE(Blueprintable)
class UPooledActorInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class DYNAMICOBJECTPOOLER_API IPooledActorInterface
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Dynamic Object Pooling")
	void ResetPooledActor();
};
