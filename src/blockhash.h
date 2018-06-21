
#ifndef _LBTC_BLOCK_HASH_H_
#define _LBTC_BLOCK_HASH_H_

#include <cstddef>
#include <boost/functional/hash.hpp>
#include "stdint.h"
#include "uint256.h"
#include "_blockhash.h"

#define CHECK_START_HEIGHT 500000
#define CHECK_END_HEIGHT 1334360

bool IsNeedCheckBlockHashHash(uint64_t height)
{
	if(height >= CHECK_START_HEIGHT && height <= CHECK_END_HEIGHT)
		return true;
	return false;
}

bool CheckBlockHashHash(uint256 hash, uint64_t height)
{
	if(!(height >= CHECK_START_HEIGHT && height <= CHECK_END_HEIGHT))
		return true;

	return hash.GetCheapHash() == *(uint64_t*)(void*)(&ablockhash[(height - CHECK_START_HEIGHT) * 8]);
}

#endif
