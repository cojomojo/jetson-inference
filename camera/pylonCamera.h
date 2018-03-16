
#ifndef __PYLON_CAMERA_H__
#define __PYLON_CAMERA_H__

#include <string>
#include "camera.h"


/**
 * Basler Pylon USB 3.0 camera.
 */
class pylonCamera : public camera
{
public:
	pylonCamera(int height, int width);
	~pylonCamera();

	bool Open();
	void Close();
	// Capture RGB
	bool Capture( void** cpu, void** cuda, unsigned long timeout=ULONG_MAX );

private:
    std::unique_ptr<Pylon::CInstantCameraArray> cameras_;
    std::mutex retrieve_mutex_;
};

#endif
