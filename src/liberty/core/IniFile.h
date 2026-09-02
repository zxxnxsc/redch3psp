#pragma once

#ifdef RW_PSP
#define DEFAULT_MAX_NUMBER_OF_PEDS 12.0f
#define DEFAULT_MAX_NUMBER_OF_CARS 8.0f
#else
#define DEFAULT_MAX_NUMBER_OF_PEDS 25.0f
#define DEFAULT_MAX_NUMBER_OF_CARS 12.0f
#endif

class CIniFile
{
public:
	static void LoadIniFile();

	static float PedNumberMultiplier;
	static float CarNumberMultiplier;
};
