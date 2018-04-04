
#ifndef __PYLON_CAMERA_H__
#define __PYLON_CAMERA_H__

#include <memory>
#include <mutex>
#include <pylon/PylonIncludes.h>
#include <string>
#include "camera.h"
#include "CameraNode.h"
#include "cudaMappedRingBuffer.h"
#include "ringBuffer.h"


/**
 * Basler Pylon USB 3.0 camera.
 */
class pylonCamera : public camera
{
public:
	pylonCamera(std::vector<CameraNode*> cameras,
				int height,
				int width,
				int framerate,
				int buffer_size = 16);
	~pylonCamera();

	bool Open();
	void Close();
	// Capture RGB
	bool Capture(void** cpu, void** cuda, unsigned long timeout=ULONG_MAX);

protected:
	bool StartGrabbing();
	bool StartRetrieveLoop(unsigned long timeout);
	void RetrieveLoop(unsigned long timeout);
	bool RetrieveImage(unsigned long timeout);

private:
    Pylon::CInstantCameraArray* mCameras;
	Pylon::EPixelType mPixelType;
	int mFramerate;
	Pylon::CPylonImage mNextImage;
    std::mutex mRetrieveMutex;

	bool mContinueRetrieving = true;
	bool mIsRetrieving = false;

	std::unique_ptr<RingBuffer<void*>> mBufferCPU;
	std::unique_ptr<CudaMappedRingBuffer<void*>> mBufferGPU;
};

#endif
