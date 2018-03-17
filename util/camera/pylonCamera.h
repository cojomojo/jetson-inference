
#ifndef __PYLON_CAMERA_H__
#define __PYLON_CAMERA_H__

#include <memory>
#include <mutex>
#include <pylon/PylonIncludes.h>
#include <string>
#include "camera.h"
#include "CameraNode.h"


/**
 * Basler Pylon USB 3.0 camera.
 */
class pylonCamera : public camera
{
public:
	pylonCamera(std::vector<CameraNode*> cameras,
				int height,
				int width);
	~pylonCamera();

	bool Open();
	void Close();
	// Capture RGB
	bool Capture(void** cpu, void** cuda, unsigned long timeout);

	bool StartGrabbing();

private:
    Pylon::CInstantCameraArray* mCameras;
	Pylon::CPylonImage mNextImage;
    std::mutex mRetrieveMutex;
};

#endif
