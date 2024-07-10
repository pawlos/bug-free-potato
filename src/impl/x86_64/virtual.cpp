#include "virtual.h"

void memset(void* dst, pt::uint64_t value, pt::size_t size)
{
	if (size < 8)
	{
		const auto* valuePtr = (pt::uint8_t*)&value;
		for (auto* ptr = (pt::uint8_t*)dst; ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
		{
			*ptr = *valuePtr;
			valuePtr++;
		}

		return;
	}

	const pt::uint64_t proceedingBytes = size % 8;
	const pt::uint64_t newnum = size - proceedingBytes;

	for (auto* ptr = (pt::uint64_t*)dst; ptr < (pt::uint64_t*)((pt::uint64_t)dst + size); ptr++)
	{
		*ptr = value;
	}

	auto* valPtr = (pt::uint8_t*)&value;
	for (auto* ptr = (pt::uint8_t*)((pt::uint64_t)dst+newnum); ptr < (pt::uint8_t*)((pt::uint64_t)dst + size); ptr++)
	{
		*ptr = *valPtr;
		valPtr++;
	}
}

void* VMM::kmalloc(pt::size_t size)
{
	klog("[VMM] Allocating %d bytes memory.\n", size);
	const pt::uint64_t remainder = size % 8;
	size -= remainder;
	if (remainder != 0) size += 8;

	kMemoryRegion* currentMemorySegment = this->firstFreeMemoryRegion;

	while (true)
	{
		if (currentMemorySegment->length >= size)
		{
			if (currentMemorySegment->length > size + sizeof(kMemoryRegion))
			{
				auto* newMemoryRegion =
						(kMemoryRegion*)((pt::uint64_t) currentMemorySegment +
										sizeof(kMemoryRegion) + size);
				newMemoryRegion->free = true;
				newMemoryRegion->length = ((pt::uint64_t)currentMemorySegment->length) - (sizeof(kMemoryRegion) + size);
				newMemoryRegion->nextFreeChunk = currentMemorySegment->nextFreeChunk;
				newMemoryRegion->nextChunk = currentMemorySegment->nextChunk;
				newMemoryRegion->prevChunk = currentMemorySegment;
				newMemoryRegion->prevFreeChunk = currentMemorySegment->prevFreeChunk;

				currentMemorySegment->nextFreeChunk = newMemoryRegion;
				currentMemorySegment->nextChunk = newMemoryRegion;
				currentMemorySegment->length = size;
			}
			if (currentMemorySegment == firstFreeMemoryRegion)
			{
				firstFreeMemoryRegion = currentMemorySegment->nextFreeChunk;
			}
			currentMemorySegment->free = false;


			if (currentMemorySegment->prevFreeChunk != nullptr)
				currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment->nextFreeChunk;
			if (currentMemorySegment->nextFreeChunk != nullptr)
				currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment->prevFreeChunk;
			if (currentMemorySegment->prevChunk != nullptr)
				currentMemorySegment->prevChunk->nextFreeChunk = currentMemorySegment->nextChunk;
			if (currentMemorySegment->nextChunk != nullptr)
				currentMemorySegment->nextChunk->prevFreeChunk = currentMemorySegment->prevChunk;

			return currentMemorySegment + 1;
		}
		if (currentMemorySegment->nextFreeChunk == nullptr)
		{
			kernel_panic("Not able to allocated more memory.", NotAbleToAllocateMemory);
		}
		currentMemorySegment = currentMemorySegment->nextFreeChunk;
	}
	kernel_panic("Not able to allocate memory.", NotAbleToAllocateMemory);
}

void* VMM::kcalloc(const pt::size_t size)
{
	klog("[VMM] Callocing %d bytes memory.\n", size);
	pt::uint64_t remainder = size % 8;
	void* ptr = kmalloc(size);
	memset(ptr, '\0', size);
	return ptr;
}

void combineFreeSegments(kMemoryRegion* a, kMemoryRegion* b)
{
	if (a == nullptr) return;
	if (b == nullptr) return;

	if (a < b)
	{
		a->length += b->length + sizeof(kMemoryRegion);
		a->nextChunk = b->nextChunk;
		a->nextFreeChunk = b->nextFreeChunk;
		b->nextChunk->prevChunk = a;
		b->nextChunk->prevFreeChunk = a;
		b->nextFreeChunk->prevFreeChunk = a;
	}
	else
	{
		b->length += a->length + sizeof(kMemoryRegion);
		b->nextChunk = a->nextChunk;
		b->nextFreeChunk = a->nextFreeChunk;
		a->nextChunk->prevChunk = b;
		a->nextChunk->prevFreeChunk = b;
		a->nextFreeChunk->prevFreeChunk = b;
	}
}

void VMM::kfree(void *address)
{
	kMemoryRegion* currentMemorySegment = static_cast<kMemoryRegion *>(address) - 1;
	currentMemorySegment->free = true;

	klog("[VMM] Freeing bytes memory at %p.\n", address);
	if (currentMemorySegment < firstFreeMemoryRegion)
	{
		firstFreeMemoryRegion = currentMemorySegment;
	}

	if (currentMemorySegment->nextFreeChunk != nullptr)
	{
		if (currentMemorySegment->nextFreeChunk->prevFreeChunk < currentMemorySegment)
		{
			currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment;
		}
	}
	if (currentMemorySegment->prevFreeChunk != nullptr)
	{
		if (currentMemorySegment->prevFreeChunk->nextFreeChunk > currentMemorySegment)
		{
			currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment;
		}
	}
	if (currentMemorySegment->nextChunk != nullptr)
	{
		currentMemorySegment->nextChunk->prevChunk = currentMemorySegment;
		if (currentMemorySegment->nextChunk->free)
			combineFreeSegments(currentMemorySegment, currentMemorySegment->nextChunk);
	}
	if (currentMemorySegment->prevChunk != nullptr)
	{
		currentMemorySegment->prevChunk->nextChunk = currentMemorySegment;
		if (currentMemorySegment->prevChunk->free)
			combineFreeSegments(currentMemorySegment, currentMemorySegment->prevChunk);
	}
}