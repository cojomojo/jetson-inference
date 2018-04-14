#ifndef __CUDA_MAPPED_RING_BUFFER_H__
#define __CUDA_MAPPED_RING_BUFFER_H__

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include "cudaMappedMemory.h"

/**
 * Implementation of a ring (circular) buffer with CUDA zero-copy mapped memory.
 */
template <class T>
class CudaMappedRingBuffer
{

public:
    /**Creates a new CudaMappedRingBuffer of the size provided.
     * The implementation utilizes an empty buffer position to distinguish full from empty, so the available position in
     * the buffer is actually the caller provided size-1.
     */
    CudaMappedRingBuffer(size_t size, size_t stride = 1)
        : mSize(size), mStride(stride),
          mCPUBuffer(std::unique_ptr<T[]>(new T[size])),
          mGPUBuffer(std::unique_ptr<T[]>(new T[size]))
    {
        for (uint32_t i = 0; i < Size(); i++)
		{
			if(!cudaAllocMapped(&mCPUBuffer[i], &mGPUBuffer[i], size*stride) )
				printf(LOG_CUDA "failed to allocate CudaMappedRingBuffer %u (size=%lu)\n", i, size*stride);
		}
    }

    std::tuple<T, T> Get();
    void Put(T item);
    /** memcpys the item into the next CudaMappedRingBuffer spot.
     */
    void Copy(T item, size_t size);
    void Reset();
    bool IsFull();
    bool IsEmpty();
    size_t Size();

protected:
    std::unique_ptr<T[]> mCPUBuffer;
    std::unique_ptr<T[]> mGPUBuffer;

private:
    std::mutex mMutex;
    size_t mHead = 0;
    size_t mTail = 0;
    size_t mSize;
    size_t mStride;
};


template<typename T>
std::tuple<T, T> CudaMappedRingBuffer<T>::Get()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (IsEmpty())
        return std::make_tuple(T(), T());

    T cpu = mCPUBuffer[mTail];
    T gpu = mGPUBuffer[mTail];
    mTail = (mTail+1) % mSize;

    return std::make_tuple(cpu, gpu);
}


template<typename T>
void CudaMappedRingBuffer<T>::Put(T item)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mCPUBuffer[mHead] = item;
    mHead = (mHead+1) % mSize;

    // In the case that we have wrapped around, increment the tail.
    // This keeps one buffer entry always open.
    if (mHead == mTail)
        mTail = (mTail+1) % mSize;
}

template<typename T>
void CudaMappedRingBuffer<T>::Copy(T item, size_t size)
{
    std::lock_guard<std::mutex> lock(mMutex);

    memcpy(mCPUBuffer[mHead], item, size);
    mHead = (mHead+1) % mSize;

    // In the case that we have wrapped around, increment the tail.
    // This keeps one buffer entry always open.
    if (mHead == mTail)
        mTail = (mTail+1) % mSize;
}


template<typename T>
void CudaMappedRingBuffer<T>::Reset()
{
    std::lock_guard<std::mutex> lock(mMutex);
    mHead = mTail;
}

template<typename T>
bool CudaMappedRingBuffer<T>::IsEmpty()
{
    return mHead == mTail;
}


template<typename T>
bool CudaMappedRingBuffer<T>::IsFull()
{
    return ((mHead+1) % mSize) == mTail;
}

template<typename T>
size_t CudaMappedRingBuffer<T>::Size()
{
    return mSize;
}

#endif
