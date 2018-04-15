/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "camera.h"
#include "CameraNode.h"
#include "commandLine.h"
#include "pylonCamera.h"

#include "glDisplay.h"
#include "glTexture.h"

#include "cudaNormalize.h"
#include "cudaFont.h"
#include "imageNet.h"

#define DEBUG
#define SLOW_DEMO_MODE 0

#ifdef DEBUG
#  define LOG_ERROR(x) do { std::cerr << "[error][imagenet-kiwirover]  " << x; } while (0)
#  define LOG_WARN(x) do { std::cout << "[warning][imagenet-kiwirover]  " << x; } while (0)
#  define LOG_MSG(x) do { std::cout << "[message][imagenet-kiwirover]  " << x; } while (0)
#else
#  define LOG_ERROR(x) do {} while (0)
#  define LOG_WARN(x) do {} while (0)
#  define LOG_MSG(x) do {} while (0)
#endif

bool signal_received = false;
void sig_handler(int signo);

glDisplay* display = NULL;
glTexture* texture = NULL;
cudaFont* font = NULL;
void create_opengl_display(uint32_t width, uint32_t height);
void update_opengl_display(uint32_t width, uint32_t height, void* imgRGBA);

int create_udp_socket(struct sockaddr_in* servaddr);
bool send_trigger_over_udp(int socket, struct sockaddr_in* servaddr, uint32_t cam_index);

int main( int argc, char** argv )
{
	printf("imagenet-kiwirover\n  args (%i):  ", argc);
	for( int i=0; i < argc; i++ )
		printf("%i [%s]  ", i, argv[i]);
	printf("\n\n");

	// parse CLI arguments specific to kiwirover
	commandLine cli(argc, argv);
	bool verbose = cli.GetFlag("v");
	bool xVerbose = cli.GetFlag("vv");
	bool xxVerbose = cli.GetFlag("vvv");
	bool shouldDisplay = cli.GetFlag("display");
	bool noUDPTrigger = cli.GetFlag("no-udp-trigger");

	// attach signal handler
	if( signal(SIGINT, sig_handler) == SIG_ERR )
		printf("\ncan't catch SIGINT\n");

	// create the camera device
	CameraNode cam1("22334243"); CameraNode cam2("22279978");
	CameraNode cam3("22627232");
	std::vector<CameraNode*> cameras = { &cam1, &cam2, &cam3 };
	camera* camera = new pylonCamera(cameras, 256, 256, 50, 32);

	if( !camera )
	{
		printf("\nimagenet-kiwirover:  failed to initialize video device\n");
		return 0;
	}
	printf("\nimagenet-kiwirover:  successfully initialized video device\n");
	printf("    width:  %u\n", camera->GetWidth());
	printf("   height:  %u\n", camera->GetHeight());
	printf("    depth:  %u (bpp)\n\n", camera->GetPixelDepth());

	// create the imageNet instance with the specified neural network
	imageNet* net = imageNet::Create(argc, argv);
	if( !net )
	{
		printf("imagenet-kiwirover:   failed to initialize imageNet\n");
		return 0;
	}

	// Create OpenGL stuff.
	if( shouldDisplay )
	{
		create_opengl_display(camera->GetWidth(), camera->GetHeight());
		font = cudaFont::Create();
	}

	int socket;
	struct sockaddr_in servaddr;
	if( !noUDPTrigger )
	{
		socket = create_udp_socket(&servaddr);
		if( socket < 0 )
		{
			return 0;
		}
	}

	// Start camera streaming.
	if( !camera->Open() )
	{
		printf("\nimagenet-kiwirover:  failed to open camera for streaming\n");
		return 0;
	}
	printf("\nimagenet-kiwirover:  camera open for streaming\n");


	/*
	 * processing loop
	 */
	float confidence = 0.0f;

	while( !signal_received )
	{
		void* imgCPU  = NULL;
		void* imgCUDA = NULL;

		// get the latest frame
		uint32_t camIndex = camera->Capture(&imgCPU, &imgCUDA, 100);
		if (camIndex < 0)
			printf("\nimagenet-kiwirover:  failed to capture frame\n");
		else
			printf("imagenet-kiwirover:  received new frame CAMIDX=%d  CPU=0x%p  GPU=0x%p\n", camIndex, imgCPU, imgCUDA);

		void* imgRGBA = NULL;

		if( !camera->ConvertRGBtoRGBA(imgCUDA, &imgRGBA) )
			printf("imagenet-kiwirover:  failed to convert from RGB to RGBA\n");

		// classify image
		const int img_class = net->Classify((float*)imgRGBA, camera->GetWidth(), camera->GetHeight(), &confidence);

		if( img_class >= 0 )
		{
			printf("imagenet-kiwirover:  %2.5f%% class #%i (%s)\n", confidence * 100.0f, img_class, net->GetClassDesc(img_class));

			send_trigger_over_udp(socket, &servaddr, camIndex);

			if( shouldDisplay && (font != NULL) )
			{
				char str[256];
				sprintf(str, "%05.2f%% %s", confidence * 100.0f, net->GetClassDesc(img_class));
				font->RenderOverlay((float4*)imgRGBA, (float4*)imgRGBA, camera->GetWidth(), camera->GetHeight(),
									str, 0, 0, make_float4(255.0f, 255.0f, 255.0f, 255.0f));
			}

			if( shouldDisplay && (display != NULL) )
			{
				char str[256];
				sprintf(str, "TensorRT build %x | %s | %s | %04.1f FPS", NV_GIE_VERSION, net->GetNetworkName(), net->HasFP16() ? "FP16" : "FP32", display->GetFPS());
				display->SetTitle(str);
			}
		}

		if( shouldDisplay )
			update_opengl_display(camera->GetWidth(), camera->GetHeight(), imgRGBA);

#if SLOW_DEMO_MODE
		usleep(1000*3*1000);
#endif
	}

	printf("\nimagenet-kiwirover:  un-initializing video device\n");

	// Shutdown the camera device.
	if( camera != NULL )
	{
		delete camera;
		camera = NULL;
	}

	//
	// Clean up.
	//
	if( display != NULL )
	{
		delete display;
		display = NULL;
	}
	if( texture != NULL )
	{
		delete texture;
		texture = NULL;
	}
	if( font != NULL )
	{
		delete font;
		font = NULL;
	}

	printf("imagenet-kiwirover:  video device has been un-initialized.\n");
	printf("imagenet-kiwirover:  this concludes the test of the video device.\n");

	return 0;
}


void sig_handler(int signo)
{
	if( signo == SIGINT )
	{
		printf("received SIGINT\n");
		signal_received = true;
	}
}


/**
 * Create OpenGL display.
 */
void create_opengl_display(uint32_t width, uint32_t height)
{
	display = glDisplay::Create();
	texture = NULL;

	if( !display ) {
		printf("\nimagenet-kiwirover:  failed to create openGL display\n");
	}
	else
	{
		texture = glTexture::Create(width, height, GL_RGBA32F_ARB/*GL_RGBA8*/);
		if( !texture )
			printf("imagenet-kiwirover:  failed to create openGL texture\n");
	}
}


/**
 *	Updates the OpenGL display with the new frame.
 */
void update_opengl_display(uint32_t width, uint32_t height, void* imgRGBA)
{
	// update display
	if( display != NULL )
	{
		display->UserEvents();
		display->BeginRender();

		if( texture != NULL )
		{
			// rescale image pixel intensities for display
			CUDA
			(
				cudaNormalizeRGBA((float4*)imgRGBA, make_float2(0.0f, 255.0f),
								  (float4*)imgRGBA, make_float2(0.0f, 1.0f),
								  width, height)
			);

			// map from CUDA to openGL using GL interop
			void* tex_map = texture->MapCUDA();

			if( tex_map != NULL )
			{
				cudaMemcpy(tex_map, imgRGBA, texture->GetSize(), cudaMemcpyDeviceToDevice);
				texture->Unmap();
			}

			// draw the texture
			texture->Render(100,100);
		}

		display->EndRender();
	}
}


int create_udp_socket(struct sockaddr_in* servaddr)
{
	int soc;

	if( (soc = socket(AF_INET, SOCK_DGRAM, 0)) < 0 )
	{
		LOG_ERROR("Cannot create UDP socket.\n");
		return -1;
	}

	LOG_MSG("Created UDP socket.\n");

	memset((char*) servaddr, 0, sizeof(*servaddr));
	servaddr->sin_family = AF_INET;
	servaddr->sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr->sin_port = htons(5005);

	struct hostent* hp;
	const char* host = "cbalos-desktop.local";

	hp = gethostbyname(host);
	if( !hp )
	{
		LOG_ERROR("Could not obtain address of " << host << std::endl);
		return -1;
	}

	LOG_MSG("Established USP connection to " << host << std::endl);

	memcpy((void*) &servaddr->sin_addr, hp->h_addr_list[0], hp->h_length);

	return soc;
}


bool send_trigger_over_udp(int socket, struct sockaddr_in* servaddr, uint32_t cam_index)
{
	std::string msg = std::to_string(cam_index);
	if ( sendto(socket, msg.c_str(), msg.length(), 0,  (struct sockaddr*) servaddr, sizeof(*servaddr)) < 0 )
	{
		LOG_ERROR("Failed to send trigger packet.\n");
		return false;
	}
	LOG_MSG("Sent trigger packet.\n");
	return true;
}
