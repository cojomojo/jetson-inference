
#include <pylon/PylonIncludes.h>
#include "pylonCamera.h"

// constructor
pylonCamera::pylonCamera(std::vector<std::shared_ptr<CameraNode>> cameras,
                         int height,
                         int width)
    : camera(height, width)
{
    // Initialize Pylon runtime first.
    Pylon::PylonInitialize();
    Open(cameras);
}


CameraAPI::~CameraAPI()
{
    // Properly delete CInstanceCameraArray
    cameras_.reset();
    // Terminate the Pylon runtime.
    Pylon::PylonTerminate();
}


bool pylonCamera::Open(std::vector<std::shared_ptr<CameraNode>> cameras)
{
    try
    {
        // Create the CInstantCameraArray.
        auto instantCams = std::make_unique<Pylon::CInstantCameraArray>(cameras.size());

        // First attach all the devices according to their serial numbers and defined index.
        for (auto i = 0; i < instantCams->GetSize(); ++i)
        {
            Pylon::CDeviceInfo deviceInfo;
            deviceInfo.SetSerialNumber(cameras[i]->serialNumber.c_str());
            (*instantCams)[i].Attach(Pylon::CTlFactory::GetInstance().CreateFirstDevice(deviceInfo));
            std::cout << "Attached device " << (*instantCams)[i].GetDeviceInfo().GetSerialNumber()
                << " (" << cameras[i]->description << ")" << " as index " << i << std::endl;
        }

        // Open all the cameras.
        instantCams->Open();
        cameras_ = std::move(instantCams);

        // Configure cameras.
        for (auto i = 0; i < cameras_->GetSize(); ++i)
        {
            if (GenApi::IsAvailable((*cameras_)[i].GetNodeMap().GetNode("AcquisitionFrameRateEnable")))
                GenApi::CBooleanPtr((*cameras_)[i].GetNodeMap().GetNode("AcquisitionFrameRateEnable"))->SetValue(true);
            if (GenApi::IsAvailable((*cameras_)[i].GetNodeMap().GetNode("AcquisitionFrameRate")))
                // BCON and USB use SFNC3 names.
                GenApi::CFloatPtr((*cameras_)[i].GetNodeMap().GetNode("AcquisitionFrameRate"))->SetValue(framerate_);
        }

        // Initialize next_image_ as an empty image.
        // TODO: Allow image width/height to provided in config.xml.
        next_image_.Reset(Pylon::EPixelType::PixelType_RGB8packed, image_width_, image_height_);

        return true;
    }
    catch (GenICam::GenericException &e)
    {
        std::cerr << "An exception occurred in CameraAPI::Initialize: " << std::endl
            << e.GetDescription() << e.GetSourceFileName() << std::endl;
        return false;
    }
    catch (std::exception &e)
    {
        std::cerr << "An exception occurred in CameraAPI::Initialize: " << std::endl << e.what() << std::endl;
        return false;
    }
}

bool pylonCamera::Close()
{
    try
	{
		std::cout << "Stopping Camera image acquisition and Pylon image grabbing..." << std::endl;
		cameras_->StopGrabbing();
        cameras_->Close();
		return true;
	}
	catch (Pylon::GenericException &e)
	{
		std::cerr << "An exception occurred in StopCamera(): " << std::endl << e.GetDescription() << std::endl;
		return false;

	}
	catch (std::exception &e)
	{
		std::cerr << "An exception occurred in StopCamera(): " << std::endl << e.what() << std::endl;
		return false;
	}
}


bool pylonCamera::Capture(void** cpu, void** cuda, unsigned long timeout=ULONG_MAX)
{
    try
    {
        // Pylon grab functions are not thread safe, so need to lock RetrieveImage since it could be called
        // from NeedDataCB or grab thread created in StartCameraLoop.
        retrieve_mutex_.lock();

        Pylon::CImageFormatConverter formatConverter;
        if (!cameras_->IsGrabbing())
        {
            std::cout << "Cameras are not grabbing. Call StartCameras() first." << std::endl;
            return false;
        }

        static int cam_index = 0;

        Pylon::CGrabResultPtr grabResult;
        (*cameras_)[cam_index].RetrieveResult(timeout/1000, grabResult, Pylon::ETimeoutHandling::TimeoutHandling_ThrowException);
        if (grabResult->GrabSucceeded())
        {
            if (!formatConverter.ImageHasDestinationFormat(grabResult))
                formatConverter.Convert(next_image_, grabResult);
            else if (formatConverter.ImageHasDestinationFormat(grabResult))
                next_image_.CopyImage(grabResult);

            intptr_t cameraContextValue = grabResult->GetCameraContext();
            std::cout << "Grabbed image from camera " << cameraContextValue << std::endl;
        }
        else
        {
            intptr_t cameraContextValue = grabResult->GetCameraContext();
            std::cout << "Pylon: Grab result failed for camera " << cameraContextValue << ": "
                << grabResult->GetErrorDescription() << std::endl;
        }

        if( cpu != NULL )
            *cpu = next_image_.GetBuffer();
        if( cuda != NULL )
            *cuda = next_image_.GetBuffer();

        // Increment camera index no matter what. This way if a camera fails, all cameras are not stalled.
        cam_index = ((cam_index+1) < cameras_->GetSize()) ? cam_index+1 : 0;

        retrieve_mutex_.unlock();
        return true;
    }
    catch (Pylon::GenericException &e)
    {
        std::cerr << "An exception occurred in RetrieveImage(): " << std::endl << e.GetDescription() << std::endl;
        retrieve_mutex_.unlock();
        return false;
    }
    catch (std::exception &e)
    {
        std::cerr << "An exception occurred in RetrieveImage(): " << std::endl << e.what() << std::endl;
        retrieve_mutex_.unlock();
        return false;
    }
}