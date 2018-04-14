#ifndef __RING_BUFFER_H__
#define __RING_BUFFER_H__

#include <cstddef>
#include <memory>
#include <mutex>
#include <tuple>

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
        : mSize(size), mStride(stride), mBuffer(std::unique_ptr<T[]>(new T[size]))
    {
    }

    T Get();
    void Put(T item);
    /** memcpys the item into the next ringbuffer spot.
     */
    void Copy(T item, size_t size);
    void Reset();
    bool IsFull();
    bool IsEmpty();
    size_t Size();

protected:
    std::unique_ptr<T[]> mBuffer;

private:
    std::mutex mMutex;
    size_t mHead = 0;
    size_t mTail = 0;
    size_t mSize;
    size_t mStride;
};


template<typename T>
T RingBuffer<T>::Get()
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (IsEmpty())
        return T();

    T value = mBuffer[mTail];
    mTail = (mTail+1) % mSize;

    return value;
}


template<typename T>
void RingBuffer<T>::Put(T item)
{
    std::lock_guard<std::mutex> lock(mMutex);

    mBuffer[mHead] = item;
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

    memcpy(mBuffer[mHead], item, size);
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
