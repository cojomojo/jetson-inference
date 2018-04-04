
#ifndef __CUDA_MAPPED_RING_BUFFER_H__
#define __CUDA_MAPPED_RING_BUFFER_H__


#include <cstddef>
#include <memory>
#include <mutex>
#include "cudaMappedMemory.h"
#include "ringBuffer.h"

/**
 * Implementation of a ring (circular) buffer.
 */
template <class T>
class CudaMappedRingBuffer : public RingBuffer<T>
{
public:
    /**Creates a new CudaRingBuffer of the size provided.
     * The implementation utilizes an empty buffer position to distinguish full from empty, so the available position in
     * the buffer is actually the caller provided size-1.
     */
    CudaMappedRingBuffer(size_t size, RingBuffer<T>* cpuRingBuffer)
        : RingBuffer<T>(size)
    {
        for (uint32_t i = 0; i < this->Size(); i++)
		{
			if(!cudaAllocMapped(cpuRingBuffer->Ptr(), &this->mBuffer[i], size) )
				printf(LOG_CUDA "failed to allocate ringbuffer %u (size=%u)\n", i, size);
		}
    }

};

#endif
