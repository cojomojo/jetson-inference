
#include "pylonCamera.h"
#include "pylonUtility.h"

// constructor
pylonCamera::pylonCamera(std::vector<std::string> camera_serials,
                         int height,
                         int width,
                         int framerate,
                         int buffer_size)
    : camera(height, width), mFramerate(framerate), mImageSavePath(""), mCaptureCount(0)
{
    // Initialize Pylon runtime first.
    Pylon::PylonInitialize();

    // Create the CInstantCameraArray.
    mCameras = new Pylon::CInstantCameraArray(camera_serials.size());

    // First attach all the devices according to their serial numbers and defined index.
    for (auto i = 0; i < mCameras->GetSize(); ++i)
    {
        Pylon::CDeviceInfo deviceInfo;
        deviceInfo.SetSerialNumber(camera_serials[i].c_str());
        (*mCameras)[i].Attach(Pylon::CTlFactory::GetInstance().CreateFirstDevice(deviceInfo));
        std::cout << LOG_PYLON << "Attached device " << (*mCameras)[i].GetDeviceInfo().GetSerialNumber()
            << " as index " << i << std::endl;
    }

    // TODO: un-hardcode mPixelType
    // mPixelType = Pylon::EPixelType::PixelType_YUV422_YUYV_Packed;
    mPixelType = Pylon::EPixelType::PixelType_RGB8packed;

    // Initialize mNextImage as an empty image.
    mNextImage.Reset(mPixelType, mWidth, mHeight);
    mDepth = Pylon::BitDepth(mPixelType);
    mSize = mNextImage.GetImageSize();

    mCamIndexBuffer = std::unique_ptr<RingBuffer<uint32_t>>(new RingBuffer<uint32_t>(buffer_size));
    mCudaMappedBuffer = std::unique_ptr<CudaMappedRingBuffer<void*>>(new CudaMappedRingBuffer<void*>(buffer_size, mSize));
}


pylonCamera::~pylonCamera()
{
    // Make sure to close cameras first.
    Close();
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
            GenApi::INodeMap& nodemap = (*mCameras)[i].GetNodeMap();
            if (GenApi::IsAvailable(nodemap.GetNode("Width")))
            {
                GenApi::CIntegerPtr(nodemap.GetNode("Width"))
                    ->SetValue(mWidth);
            }
            if (GenApi::IsAvailable(nodemap.GetNode("Height")))
            {
                GenApi::CIntegerPtr(nodemap.GetNode("Height"))
                    ->SetValue(mHeight);
            }
            if (GenApi::IsAvailable(nodemap.GetNode("AcquisitionFrameRateEnable")))
            {
                GenApi::CBooleanPtr(nodemap.GetNode("AcquisitionFrameRateEnable"))
                    ->SetValue(true);
            }
            if (GenApi::IsAvailable(nodemap.GetNode("AcquisitionFrameRate")))
            {
                // BCON and USB use SFNC3 names.
                GenApi::CFloatPtr(nodemap.GetNode("AcquisitionFrameRate"))
                    ->SetValue(10); // TODO: Unhardcode framerate
            }

            GenApi::CEnumerationPtr pixelFormat(nodemap.GetNode("PixelFormat"));
            if (GenApi::IsAvailable(pixelFormat->GetEntry(mPixelType)))
            {
                std::cout << LOG_PYLON << "Changing pixel format from " << pixelFormat->ToString()
                    << " to " << mPixelType << std::endl;
                pixelFormat->SetIntValue(mPixelType);
            }
        }

	    // Start the camera frame grabbing.
	    StartGrabbing();

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
		std::cout << LOG_PYLON << "Stopping Camera image acquisition and Pylon image grabbing..." << std::endl;\
        std::unique_lock<std::mutex> lock(mContinueMutex);
        mContinueRetrieving = false;
        lock.unlock();
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


uint32_t pylonCamera::Capture(void** cpu, void** cuda, unsigned long timeout)
{
    if (!mIsRetrieving)
        mIsRetrieving = StartRetrieveLoop(timeout);

    while (mCudaMappedBuffer->IsEmpty());

    std::tuple<void*, void*> entry = mCudaMappedBuffer->Get();
    if (cpu != NULL)
        *cpu = std::get<0>(entry);
    if (cuda != NULL)
        *cuda = std::get<1>(entry);

    mCaptureCount++;

    return mCamIndexBuffer->Get();
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

bool pylonCamera::StartRetrieveLoop(unsigned long timeout)
{
    retrieve_thread = std::thread(&pylonCamera::RetrieveLoop, this, timeout);
    return true;
}

bool pylonCamera::RetrieveImage(unsigned long timeout)
{
    try
    {
        // Pylon grab functions are not thread safe, so need to lock RetrieveImage since it could be called
        // from NeedDataCB or grab thread created in StartCameraLoop.
        std::lock_guard<std::mutex> lock(mRetrieveMutex);

        std::cout << LOG_PYLON << "RetrieveImage" << std::endl;

        if (!mCameras->IsGrabbing())
        {
            std::cout << LOG_PYLON << "Cameras are not grabbing. Call StartGrabbing() first." << std::endl;
            return false;
	    }

        static int cam_index = 0;

        Pylon::CGrabResultPtr grabResultPtr;
        (*mCameras)[cam_index].RetrieveResult(timeout, grabResultPtr, Pylon::ETimeoutHandling::TimeoutHandling_ThrowException);
        if (grabResultPtr->GrabSucceeded())
        {
            Pylon::EPixelType pxType = grabResultPtr->GetPixelType();
            intptr_t cameraContextValue = grabResultPtr->GetCameraContext();
            std::cout << LOG_PYLON << "Grabbed image from camera " << cameraContextValue
                << " (pixelType = " << pxType << ")" << std::endl;
        }
        else
        {
            intptr_t cameraContextValue = grabResultPtr->GetCameraContext();
            std::cout << LOG_PYLON << "Grab result failed for camera " << cameraContextValue << ": "
                << grabResultPtr->GetErrorDescription() << std::endl;
        }

        mCamIndexBuffer->Put(cam_index);
        mCudaMappedBuffer->Copy(grabResultPtr->GetBuffer(), mSize);

	    // Increment camera index no matter what. This way if a camera fails, all cameras are not stalled.
        cam_index = (cam_index+1) % mCameras->GetSize();

        return true;
    }
    catch (Pylon::GenericException &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in RetrieveImage(): " << std::endl << e.GetDescription() << std::endl;
        return false;
    }
    catch (std::exception &e)
    {
        std::cerr << LOG_PYLON << "An exception occurred in RetrieveImage(): " << std::endl << e.what() << std::endl;
        return false;
    }
}

void pylonCamera::RetrieveLoop(unsigned long timeout)
{
    std::cout << LOG_PYLON << "Starting image retrieval loop" << std::endl;
    while (true)
    {
        std::unique_lock<std::mutex> lock(mContinueMutex);
        if (!mContinueRetrieving)
            break;
        lock.unlock();
        RetrieveImage(timeout);
    }
}