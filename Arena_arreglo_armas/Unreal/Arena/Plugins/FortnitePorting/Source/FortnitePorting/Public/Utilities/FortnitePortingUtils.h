#pragma once
#include <functional>
#include <optional>

#include "JsonObjectConverter.h"

class FFortnitePortingUtils
{
public:
	static FString BytesToString(TArray<uint8> Bytes)
	{
		// The app sends UTF-8 JSON; decoding byte by byte broke accented display names
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converter.Length(), Converter.Get());
	}
	
	template<typename T>
	static TOptional<T> FirstOrNull(TArray<T> Items, std::function<bool(const T&)> Predicate)
	{
		for (const auto& Item : Items)
		{
			if (Predicate(Item))
			{
				return Item;
			}
		}
		
		return T();
	}
	
	template<typename T>
	static bool Any(TArray<T> Items, std::function<bool(const T&)> Predicate)
	{
		for (const auto& Item : Items)
		{
			if (Predicate(Item))
			{
				return true;
			}
		}
		
		return false;
	}
};
