#include "virtual.h"


void* VMM::kmalloc(size_t size)
{
	uint64_t remainder = size % 8;
	size -= remainder;
	if (remainder != 0) size += 8;

	kMemoryRegion* currentMemorySegment = this->firstFreeMemoryRegion;

	while (true)
	{
		if (currentMemorySegment->length >= size)
		{
			if (currentMemorySegment->length > size + sizeof(kMemoryRegion))
			{
				kMemoryRegion* newMemoryRegion =
						(kMemoryRegion*)((uint64_t) currentMemorySegment +
										sizeof(kMemoryRegion) + size);
				newMemoryRegion->free = true;
				newMemoryRegion->length = ((uint64_t)currentMemorySegment->length) - (sizeof(kMemoryRegion) + size);
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


			if (currentMemorySegment->prevFreeChunk != NULL)
				currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment->nextFreeChunk;
			if (currentMemorySegment->nextFreeChunk != NULL)
				currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment->prevFreeChunk;
			if (currentMemorySegment->prevChunk != NULL)
				currentMemorySegment->prevChunk->nextFreeChunk = currentMemorySegment->nextChunk;
			if (currentMemorySegment->nextChunk != NULL)
				currentMemorySegment->nextChunk->prevFreeChunk = currentMemorySegment->prevChunk;

			return currentMemorySegment + 1;
		}
		if (currentMemorySegment->nextFreeChunk == NULL)
		{
			kernel_panic("Not able to allocated more memory.", NotAbleToAllocateMemory);
		}
		currentMemorySegment = currentMemorySegment->nextFreeChunk;
	}
	kernel_panic("Not able to allocate memory.", NotAbleToAllocateMemory);
}

void combineFreeSegments(kMemoryRegion* a, kMemoryRegion* b)
{
	if (a == NULL) return;
	if (b == NULL) return;

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
	kMemoryRegion* currentMemorySegment = ((kMemoryRegion*)address) - 1;

	currentMemorySegment->free = true;

	if (currentMemorySegment < firstFreeMemoryRegion)
	{
		firstFreeMemoryRegion = currentMemorySegment;
	}

	if (currentMemorySegment->nextFreeChunk != NULL)
	{
		if (currentMemorySegment->nextFreeChunk->prevFreeChunk < currentMemorySegment)
		{
			currentMemorySegment->nextFreeChunk->prevFreeChunk = currentMemorySegment;
		}
	}
	if (currentMemorySegment->prevFreeChunk != NULL)
	{
		if (currentMemorySegment->prevFreeChunk->nextFreeChunk > currentMemorySegment)
		{
			currentMemorySegment->prevFreeChunk->nextFreeChunk = currentMemorySegment;
		}
	}
	if (currentMemorySegment->nextChunk != NULL)
	{
		currentMemorySegment->nextChunk->prevChunk = currentMemorySegment;
		if (currentMemorySegment->nextChunk->free)
			combineFreeSegments(currentMemorySegment, currentMemorySegment->nextChunk);
	}
	if (currentMemorySegment->prevChunk != NULL)
	{
		currentMemorySegment->prevChunk->nextChunk = currentMemorySegment;
		if (currentMemorySegment->prevChunk->free)
			combineFreeSegments(currentMemorySegment, currentMemorySegment->prevChunk);
	}
}