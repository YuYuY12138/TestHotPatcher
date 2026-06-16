#pragma once
#include "Misc/SecureHash.h"
#include "Containers/UnrealString.h"

/**
 * UE 5.7 适配版 MD5Wrapper
 * 用引擎内置 FMD5 替代 OpenSSL，无需 SSL 模块依赖
 */
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

	inline const char* Final()
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
		return TCHAR_TO_ANSI(*Md5String);
	}

	inline const char* GetMd5() const
	{
		return bFinaled ? TCHAR_TO_ANSI(*Md5String) : nullptr;
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