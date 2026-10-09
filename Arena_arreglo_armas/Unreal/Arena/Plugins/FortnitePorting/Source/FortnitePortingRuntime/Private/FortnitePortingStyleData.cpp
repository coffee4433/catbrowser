#include "FortnitePortingStyleData.h"

bool UFortnitePortingStyleData::HasChoices() const
{
	for (const FFortnitePortingStyleChannel& Channel : Channels)
	{
		if (Channel.Options.Num() > 1)
		{
			return true;
		}
	}
	return false;
}

TArray<int32> UFortnitePortingStyleData::Sanitize(const TArray<int32>& Selection) const
{
	TArray<int32> Result;
	Result.Init(0, Channels.Num());
	for (int32 Index = 0; Index < Channels.Num(); ++Index)
	{
		if (Selection.IsValidIndex(Index) && Channels[Index].Options.IsValidIndex(Selection[Index]))
		{
			Result[Index] = Selection[Index];
		}
	}
	return Result;
}
