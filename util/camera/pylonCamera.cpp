
#include "pylonCamera.h"
#include "pylonUtility.h"

// constructor
pylonCamera::pylonCamera(std::vector<CameraNode*> cameras,
                         int height,
                         int width)
    : camera(height, width)
{
    // Initialize Pylon runtime first.
    Pylon::PylonInitialize();

    // Create the CInstantCameraArray.
    mCameras = new Pylon::CInstantCameraArray(cameras.size());

    // First attach all the devices according to their serial numbers and defined index.
    for (auto i = 0; i < mCameras->GetSize(); ++i)
    {
        Pylon::CDeviceInfo deviceInfo;
        deviceInfo.SetSerialNumber(cameras[i]->serialNumber.c_str());
        (*mCameras)[i].Attach(Pylon::CTlFactory::GetInstance().CreateFirstDevice(deviceInfo));
        std::cout << LOG_PYLON << "Attached device " << (*mCameras)[i].GetDeviceInfo().GetSerialNumber()
            << " (" << cameras[i]->description << ")" << " as index " << i << std::endl;
    }
 
    // TODO: unhardcode mDepth by calculating it in Capture()
    mDepth = 3;
}


pylonCamera::~pylonCamera()
{
    // Properly delete CInstanceCameraArray
    delete mCameras;
    // Terminate the Pylon runtime.
    Pylon::PylonTerminate();
}


bool pylonCamera::Open()
{
    try
    {
        // Open all the cameras.
        mCameras->Open();

        // Configure cameras.
        for (auto i = 0; i < mCameras->GetSize(); ++i)
        {
            if (GenApi::IsAvailable((*mCameras)[i].GetNodeMap().GetNode("AcquisitionFrameRateEnable")))
                GenApi::CBooleanPtr((*mCameras)[i].GetNodeMap().GetNode("AcquisitionFrameRateEnable"))->SetValue(true);
            if (GenApi::IsAvailable((*mCameras)[i].GetNodeMap().GetNode("AcquisitionFrameRate")))
                // BCON and USB use SFNC3 names.
                GenApi::CFloatPtr((*mCameras)[i].GetNodeMap().GetNode("AcquisitionFrameRate"))->SetValue(30); // TODO: Unhardcode framerate
        }

        // Initialize mNextImage as an empty image.
        mNextImage.Reset(Pylon::EPixelType::PixelType_RGB8packed, mWidth, mHeight);

        return true;
    }
    catch (GenICam::GenericException &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in Open(): " << std::endl
            << e.GetDescription() << e.GetSourceFileName() << std::endl;
        return false;
    }
    catch (std::exception &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in Open(): " << std::endl << e.what() << std::endl;
        return false;
    }
}

void pylonCamera::Close()
{
    try
	{
		std::cout << LOG_PYLON << "Stopping Camera image acquisition and Pylon image grabbing..." << std::endl;
		mCameras->StopGrabbing();
        mCameras->Close();
	}
	catch (Pylon::GenericException &e)
	{
		std::cerr << LOG_PYLON << "An exception occurred in Close(): " << std::endl << e.GetDescription() << std::endl;
	}
	catch (std::exception &e)
	{
		std::cerr << LOG_PYLON << "An exception occurred in Close(): " << std::endl << e.what() << std::endl;
	}
}

bool pylonCamera::StartGrabbing()
{
    // Get all cameras running in free-run mode. Images will be captured and put into a queue for retrieving
    // using RetrieveImage. The available grab strategies are:
    // GrabStrategy_OneByOne
    // The images are processed in the order of their arrival. This is the default grab strategy.
    //
    // GrabStrategy_LatestImageOnly
    // Only the latest image is kept in the output queue, all other grabbed images are skipped. If no image is in the
    // output queue when retrieving an image with CInstantCamera::RetrieveResult(), the processing waits for the upcoming
    // image.
    //
    // GrabStrategy_LatestImages
    // This strategy can be used to grab images while keeping only the latest images. If the application does not
    // retrieve all images in time, all other grabbed images are skipped. The CInstantCamera::OutputQueueSize parameter
    // can be used to control how many images can be queued in the output queue. When setting the output queue size to 1,
    // this strategy is equivalent to GrabStrategy_LatestImageOnly grab strategy. When setting the output queue size to
    // CInstantCamera::MaxNumBuffer, this strategy is equivalent to GrabStrategy_OneByOne.
    //
    // GrabStrategy_UpcomingImage
    // The input buffer queue is kept empty. This prevents grabbing. However, when retrieving an image with a call to
    // the CInstantCamera::RetrieveResult() method a buffer is queued into the input queue and then the call waits for
    // the upcoming image. The buffer is removed from the queue on timeout. Hence after the call to the
    // CInstantCamera::RetrieveResult() method the input buffer queue is empty again. The upcoming image grab strategy
    // cannot be used together with USB camera devices. See the advanced topics section of the pylon Programmer's Guide
    // for more information.

    std::cout << LOG_PYLON << "Starting Pylon driver grab engine..." << std::endl;
    mCameras->StartGrabbing(Pylon::EGrabStrategy::GrabStrategy_LatestImageOnly);
    return true;
}


bool pylonCamera::Capture(void** cpu, void** cuda, unsigned long timeout=ULONG_MAX)
{
    try
    {
        // Pylon grab functions are not thread safe, so need to lock RetrieveImage since it could be called
        // from NeedDataCB or grab thread created in StartCameraLoop.
        mRetrieveMutex.lock();

        Pylon::CImageFormatConverter formatConverter;
        if (!mCameras->IsGrabbing())
        {
            std::cout << LOG_PYLON << "Cameras are not grabbing. Call StartGrabbing() first." << std::endl;
            return false;
	}

        static int cam_index = 0;

        Pylon::CGrabResultPtr grabResult;
        (*mCameras)[cam_index].RetrieveResult(timeout/1000, grabResult, Pylon::ETimeoutHandling::TimeoutHandling_ThrowException);
        if (grabResult->GrabSucceeded())
        {
            if (!formatConverter.ImageHasDestinationFormat(grabResult))
                formatConverter.Convert(mNextImage, grabResult);
            else if (formatConverter.ImageHasDestinationFormat(grabResult))
                mNextImage.CopyImage(grabResult);

            intptr_t cameraContextValue = grabResult->GetCameraContext();
            std::cout << LOG_PYLON << "Grabbed image from camera " << cameraContextValue << std::endl;
        }
        else
        {
            intptr_t cameraContextValue = grabResult->GetCameraContext();
            std::cout << LOG_PYLON << "Pylon: Grab result failed for camera " << cameraContextValue << ": "
                << grabResult->GetErrorDescription() << std::endl;
        }

        if( cpu != NULL )
            *cpu = mNextImage.GetBuffer();
        if( cuda != NULL )
            *cuda = mNextImage.GetBuffer();

        // Increment camera index no matter what. This way if a camera fails, all cameras are not stalled.
        cam_index = ((cam_index+1) < mCameras->GetSize()) ? cam_index+1 : 0;

        mRetrieveMutex.unlock();
        return true;
    }
    catch (Pylon::GenericException &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in RetrieveImage(): " << std::endl << e.GetDescription() << std::endl;
        mRetrieveMutex.unlock();
        return false;
    }
    catch (std::exception &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in RetrieveImage(): " << std::endl << e.what() << std::endl;
        mRetrieveMutex.unlock();
        return false;
    }
}
