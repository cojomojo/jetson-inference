#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>
#include "cudaMappedMemory.h"

//TODO: currently code is only allocating 16 bytes (1 byte per buffer entry). Need to allocate 16*the size of the image.
// Probably need to take two params, size of buffer in bytes and stride (bytes per element).

/**
 * Implementation of a ring (circular) buffer.
 */
template <class T>
class RingBuffer
{

public:
    /**Creates a new RingBuffer of the size provided.
     * The implementation utilizes an empty buffer position to distinguish full from empty, so the available position in
     * the buffer is actually the caller provided size-1.
     */
    RingBuffer(size_t size, size_t stride = 1)
        : mSize(size), mStride(stride),
          mCPUBuffer(std::unique_ptr<T[]>(new T[size])),
          mGPUBuffer(std::unique_ptr<T[]>(new T[size]))
    {
        for (uint32_t i = 0; i < Size(); i++)
		{
			if(!cudaAllocMapped(&mCPUBuffer[i], &mGPUBuffer[i], size*stride) )
				printf(LOG_CUDA "failed to allocate ringbuffer %u (size=%lu)\n", i, size*stride);
		}
    }

    std::tuple<T, T> Get();
    void Put(T item);
    /** memcpys the item into the next ringbuffer spot.
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
std::tuple<T, T> RingBuffer<T>::Get()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (IsEmpty())
        return std::make_tuple(T(), T());

    T cpu = mCPUBuffer[mTail];
    T gpu = mGPUBuffer[mTail];
    mTail = (mTail+1) % mSize;

    return std::make_tuple(cpu, gpu);
}

// template<typename T>
// T* RingBuffer<T>::Ptr()
// {
//     std::lock_guard<std::mutex> lock(mMutex);

//     T* ptr = &mCPUBuffer[mTail];
//     mTail = (mTail+1) % mSize;

//     return ptr;
// }

template<typename T>
void RingBuffer<T>::Put(T item)
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
void RingBuffer<T>::Copy(T item, size_t size)
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
void RingBuffer<T>::Reset()
{
    std::lock_guard<std::mutex> lock(mMutex);
    mHead = mTail;
}

template<typename T>
bool RingBuffer<T>::IsEmpty()
{
    return mHead == mTail;
}


template<typename T>
bool RingBuffer<T>::IsFull()
{
    return ((mHead+1) % mSize) == mTail;
}

template<typename T>
size_t RingBuffer<T>::Size()
{
    return mSize;
}

#endif
