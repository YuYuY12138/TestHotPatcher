#pragma once
#include "Misc/SecureHash.h"
#include "Containers/UnrealString.h"

struct FMD5Wrapper
{
	FMD5Wrapper()
	{
		Reset();
	}

	inline void Update(const void* data, size_t len)
	{
		if (!bFinaled)
		{
			Md5.Update(static_cast<const uint8*>(data), static_cast<uint64>(len));
		}
	}

	inline const FString& Final()
	{
		if (!bFinaled)
		{
			uint8 Digest[16];
			Md5.Final(Digest);
			for (int32 i = 0; i < 16; ++i)
			{
				Md5String += FString::Printf(TEXT("%02x"), Digest[i]);
			}
			bFinaled = true;
		}
		return Md5String;
	}

	inline const FString& GetMd5() const
	{
		return Md5String;
	}

	inline void Reset()
	{
		bFinaled = false;
		Md5String.Empty();
		Md5 = FMD5();
	}

private:
	FMD5 Md5;
	FString Md5String;
	bool bFinaled = false;
};
