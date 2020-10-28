#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <ifaddrs.h>
#include <SDL/SDL.h>

#include <fcntl.h> // Contains file controls like O_RDWR
#include <termios.h> // Contains POSIX terminal control definitions
#include <inttypes.h>

#include <thread>
#include <ctime>
#include <chrono>
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


#define SANITY_CHECK_INT 6969
#define SANITY_CHECK_CHAR 'a'

int tcpport = 6969;
char *ip = "192.168.88.240";


using namespace std::chrono;

#define _DEBUG 0

struct setupData 
{
	int width;
	int height;
	int port;
	int id;
	int maxerrors;
	int endian;
	int sendsteps;
	int fullscreen;
	int fullrebooterrors;
	int sanitycheck;
};



const int strip_count = 3;
const int rgb_size = 3;
const int led_count = 180;
const int total_size = rgb_size*led_count;
const int max_bright = 128;


const int strips_online = 10;

const int target_time_ms = 33;

const int render_timeout = 50; // ms timeout

struct setupData *setupInfo;

SDL_Surface *screen;
SDL_Surface *image;


unsigned char get_pixel(unsigned char* img, int x, int y, int channel)
{
	int pixel_pos;
	pixel_pos = 4 * (y * 60 + x) + channel;
	//printf("Getting pixel at %d, %d, %d: %d\n", x, y, channel, pixel_pos);
	unsigned char pix = img[pixel_pos];
	

	// Normalize
	double tmp = pix/255.0;
	pix = max_bright*tmp;

	return pix;
}
// Phys  num:   2  4  3  7  6  0  8  5  1  9
// Strip  id:   0  1  2  3  4  5  6  7  8  9
int gorder[] = {2, 4, 3, 7, 6, 0, 8, 5, 1, 9};
void write_port(int port, unsigned char **img_p, int *shouldWrite, int strip_num)
{
	unsigned char *img;

	unsigned char out[total_size];

	unsigned char in;

	//read(port, (void*)&in, 1);
	//read(port, (void*)&in, 1);
	while (1)
	{
		while (!*shouldWrite)
			usleep(500);

		// We're good to write, update our image pointer
		img = *img_p;

		read(port, (void*)&in, 1);
		printf("%c\n", in);



		strip_num = gorder[in-'0']; // TODO: Check
		printf("Strip_num: %d\n", strip_num);

		// Write to serial port
		int i = 0;
		for (int y = 0; y < 3; y++)
			for (int x = 0; x < 60; x++)
				for (int c = 0; c < 3; c++)
				{
					//printf("Setting pixel at %d\n", i);
					if (y == 1)
						out[i] = get_pixel(img, 59-x, y+(strip_num*3), c);
					else
						out[i] = get_pixel(img, x, y+(strip_num*3), c);
					i++;
				}
		write(port, out, total_size);

		*shouldWrite = 0;	
	}
}

uint64_t getCurrentMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

int open_serial_port(char *path)
{
	// Open the serial port. Change device path as needed (currently set to an standard FTDI USB-UART cable type device)
	int serial_port = open(path, O_RDWR);

	// Create new termios struc, we call it 'tty' for convention
	struct termios tty;

	// Read in existing settings, and handle any error
	if(tcgetattr(serial_port, &tty) != 0) {
	    printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
	}

	tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
	tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
	tty.c_cflag |= CS8; // 8 bits per byte (most common)
	tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
	tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO; // Disable echo
	tty.c_lflag &= ~ECHOE; // Disable erasure
	tty.c_lflag &= ~ECHONL; // Disable new-line echo
	tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

	tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed

	tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
	tty.c_cc[VMIN] = 0;

	//cfsetispeed(&tty, B230400);
	//cfsetospeed(&tty, B230400);
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	// Save tty settings, also checking for error
	if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
	    printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
	}

	return serial_port;
}

void blank(int strip)
{
	char buf[256];
	unsigned char out[180*3];

	for (int i = 0; i < 180*3; i++)
		out[i] = '\0';
	sprintf(buf, "/dev/ttyACM%d", strip);
	printf("Opening %s\n", buf);
	int port = open_serial_port(buf);

	char in;
	printf("Reading...\n");
	read(port, &in, 1);


	printf("Writing...\n");
	write(port, out, 180*3);
}


void white_strip(int strip)
{
	char buf[256];
	unsigned char out[180*3];

	for (int i = 0; i < 180*3; i++)
		out[i] = 'a';
	sprintf(buf, "/dev/ttyACM%d", strip);
	printf("Opening %s\n", buf);
	int port = open_serial_port(buf);

	char in;
	printf("Reading...\n");
	read(port, &in, 1);


	printf("Writing...\n");
	write(port, out, 180*3);
	
	printf("Wrote... Sleeping...\n");
	sleep(6);

	printf("Reading again...\n");
	read(port, &in, 1);
	
	printf("Writing...\n");
	for (int i = 0; i < 180*3; i++)
		out[i] = '\0';

	write(port, out, 180*3);

	return;

}
char *getAdapterMac(int sockfd)
{

	char *name = NULL;


	struct sockaddr_in addr;
	struct ifaddrs* ifaddr;
	struct ifaddrs* ifa;
	socklen_t addr_len;

	addr_len = sizeof (addr);
	if (getsockname(sockfd, (struct sockaddr*)&addr, &addr_len) == -1)
		printf("ERROR: Couldn't get sock name: %s", strerror(errno));
	if (getifaddrs(&ifaddr) == -1)
		printf("ERROR: Couldn't get ifaddr: %s", strerror(errno));


	// TODO: More error checking

	// look which interface contains the wanted IP.
	// When found, ifa->ifa_name contains the name of the interface (eth0, eth1, ppp0...)
	for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
	{
	    //printf("Iterating adapter...\n");
	    if (ifa->ifa_addr)
	    {
		//printf("Entering addr\n");
		if (AF_INET == ifa->ifa_addr->sa_family)
		{
		    //printf("It's IPv4\n");
		    if (ifa->ifa_name)
		    {
				name = (char*)malloc(strlen(ifa->ifa_name)+1);
				strcpy(name, ifa->ifa_name);
		    }
		}
	    }
	}
	freeifaddrs(ifaddr);


	if (name)
		printf("Got interface name: %s\n", name);
	else
		printf("ERROR: Couldn't get interface name\n");



	struct ifreq ifr;
	size_t if_name_len = strlen(name);
	if (if_name_len < sizeof(ifr.ifr_name)) 
	{
		memcpy(ifr.ifr_name, name, if_name_len);
		ifr.ifr_name[if_name_len] = 0;
	}
	else
		printf("ERROR: Interface name is too long\n");

	
	if (ioctl(sockfd, SIOCGIFHWADDR, &ifr)==-1)
		printf("ERROR: Couldn't get MAC address: %s", strerror(errno));
	
	if (ifr.ifr_hwaddr.sa_family!=ARPHRD_ETHER) 
		printf("ERROR: Only ethernet adapters supported for client...\n");


	const unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
	
	char *macout = (char*)malloc(32);
	sprintf(macout, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	printf("Got MAC address: %s\n", macout);


	return macout;
}


void init_screen_texture()
{

	Uint32 rmask, gmask, bmask, amask;
	if (setupInfo->endian)
	{
		rmask = 0x0000ff00;
		gmask = 0x00ff0000;
		bmask = 0xff000000;
		amask = 0x000000ff;
	}
	else
	{
		bmask = 0x000000ff;
		gmask = 0x0000ff00;
		rmask = 0x00ff0000;
		amask = 0xff000000;
	}


	image = SDL_CreateRGBSurface(0, setupInfo->width, setupInfo->height, 32, rmask, gmask, bmask, amask);

	if (image == NULL)
		printf("Failed to create surface :(\n");
}


void renderImage()
{

	/* Blit onto the screen surface */
	if(SDL_BlitSurface(image, NULL, screen, NULL) < 0)
		fprintf(stderr, "BlitSurface error: %s\n", SDL_GetError());
	SDL_UpdateRect(screen, 0, 0, image->w, image->h);

}




struct setupData *getSetupData(char *servAddr, int port)
{
	struct setupData *dataBuf = (struct setupData *)malloc(sizeof (struct setupData));

	int sockfd;
	struct sockaddr_in servaddr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		printf("Failed to create socket\n");
		return NULL;
	}
	bzero(&servaddr, sizeof (servaddr));
	
	
	char *mac = getAdapterMac(sockfd);

	servaddr.sin_family = AF_INET;
	//servaddr.sin_addr.s_addr = inet_addr(servAddr);
	//servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr(ip);
	servaddr.sin_port = htons(port);

	printf("Connecting to server to request setup data...\n");
	printf("Server address: %s\nPort: %d\n", servAddr, port);
	while (connect(sockfd, (struct sockaddr *) &servaddr, sizeof (servaddr)) != 0)
	{
		printf("Failed to connect to the server: %s\n", strerror(errno));
		sleep(10);
	}

	char req[256];
	req[0] = SANITY_CHECK_CHAR;
	memcpy(req+1, mac, strlen(mac)+1);
	write(sockfd, req, strlen(mac)+2);
	printf("MAGIC BYTE: %c\n", req[0]);
	read(sockfd, (char *)dataBuf, sizeof (struct setupData));


	close(sockfd);

	if (dataBuf->sanitycheck != SANITY_CHECK_INT)
	{
		printf("TCP Setup corroupt...");
		return NULL;
	}


	printf("Read data in: w: %d, h: %d, sanity: %d\n", dataBuf->width, dataBuf->height, dataBuf->sanitycheck);

	free(mac);

	return (struct setupData*)dataBuf;	
}


bool gui = true;


int main(int argc, char *argv[])
{

start:

	//while ((setupInfo = getSetupData(ip, tcpport)) == NULL); // Keep trying to get setup data until it works

	//return;

	setupInfo = (struct setupData*)malloc(sizeof (struct setupData));
	setupInfo->width = 60;
	setupInfo->height = 30;
	setupInfo->port = 6969;
	setupInfo->id = 0;
	setupInfo->maxerrors = 2;
	setupInfo->endian = 0;
	setupInfo->sendsteps =1;
	setupInfo->fullscreen = 0;
	setupInfo->fullrebooterrors = 4096;
	setupInfo->sanitycheck = 6969;

	srand(time(NULL));
	

	/* Initialize the SDL library */
	if( SDL_Init(SDL_INIT_VIDEO) < 0 ) 
	{
		fprintf(stderr,	"Couldn't initialize SDL: %s\n", SDL_GetError());
		//exit(1);

		gui = false;
	}

	if (gui)
	{

		/* Clean up on exit */
		atexit(SDL_Quit);
		
		/* Have a preference for 32-bit, but accept any depth */
		//screen = SDL_SetVideoMode(xres, yres, 32, SDL_SWSURFACE|SDL_FULLSCREEN);
		
		Uint32 vidFlags = SDL_SWSURFACE;
		if (setupInfo->fullscreen)
			vidFlags |= SDL_FULLSCREEN;

		screen = SDL_SetVideoMode(setupInfo->width, setupInfo->height, 32, vidFlags);
		if ( screen == NULL )
		{
			fprintf(stderr, "Couldn't set 640x480x8 video mode: %s\n", SDL_GetError());
			//exit(1);
			gui = false;
		}
		printf("Set 640x480 at %d bits-per-pixel mode\n",
			   screen->format->BitsPerPixel);



		init_screen_texture();
		printf("Initialized SDL!\n");
		fflush(stdout);

	}

	int sockfd;
	char *hello = "Hello from client";
	struct sockaddr_in servaddr;


	// Creating socket file descriptor
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}


	memset(&servaddr, 0, sizeof(servaddr));

	// Filling server information
	servaddr.sin_family = AF_INET;
	//servaddr.sin_port = htons(port);
	servaddr.sin_port = htons(setupInfo->port);
	servaddr.sin_addr.s_addr = inet_addr(ip);

	int n;
	unsigned int len;
			

	unsigned int recvsize = (setupInfo->width*setupInfo->height*4)/setupInfo->sendsteps;

	sleep(1);


	struct timeval tv;
	// Setup timeout
	tv.tv_sec = 0;
	tv.tv_usec = 900000; // 900ms timeout
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0)
		printf("Failed to set socket option...\n\n");


	char *imgnetbuf = (char*)malloc(recvsize+1);


	char buf[256];

	int serial_port = 0;

	int order[] = {2, 3, 7, 0, 8, 1, 9, 4, 6, 5};

	unsigned char *img = NULL;

	std::thread threads[strips_online];
	int shouldWrite[strips_online];
	for (int i = 0; i < strips_online; i++)
	{
		sprintf(buf, "/dev/ttyACM%d", i);
		printf("Opening %s\n", buf);
		serial_port = open_serial_port(buf);
		//printf("Got serial port handle: %d\n", serial_port);
		shouldWrite[i] = 0;
		threads[i] = std::thread(write_port, serial_port, &img, &shouldWrite[i], order[i]);
	}

	printf("Initialized...\n\n");

	SDL_Event event;
	
	int errorcount = 0;
	int shouldexit = 0;
	int totalfailures = 0;
	Uint32 ticks;
	while (1)
	{
		ticks = SDL_GetTicks();
	
		if (gui)
		{
			/* First check for events */
			while (SDL_PollEvent(&event))
			{
				switch (event.type)
				{
					case SDL_QUIT:
						shouldexit = 1;
						break;
					case SDL_KEYDOWN:
						if (event.key.keysym.sym == SDLK_ESCAPE)
							shouldexit = 1;

						break;	
				}
			}
		}
		if (shouldexit)
			break;


		printf("Start net: %d ms\n", SDL_GetTicks()-ticks);
		#ifdef _DEBUG
		printf("Sending req;\n");
		#endif
		int bytes_sent = sendto(sockfd, (const char *)hello, strlen(hello),
			0, (const struct sockaddr *) &servaddr,
				sizeof(servaddr));
		if (bytes_sent <= 0)
		{
			printf("Error sending: %s\n\n", strerror(errno));
		}
		#ifdef _DEBUG
		printf("Req message sent. Waiting for reply...\n");
		#endif


		if (SDL_LockSurface(image) == -1)
			printf("Failed to lock surface for writing");


		int i;
		for (i = 0; i < setupInfo->sendsteps; i++)
		{
			printf("Start recv: %d ms\n", SDL_GetTicks()-ticks);
			
			n = recvfrom(sockfd, imgnetbuf, recvsize,
				MSG_WAITALL, (struct sockaddr *) &servaddr,
				&len);
			
			printf("End recv: %d ms\n", SDL_GetTicks()-ticks);
			
			if (n == -1)
				printf("Error in recv: %s\n", strerror(errno));
			
			
			#ifdef _DEBUG
			printf("Recieved chunk %d of %u bytes :^)\n", i, n);
			#endif


			// Check recieve failed
			if (n != recvsize)
			{
				errorcount++;
				printf("ERROR: Error when recieving chunk from server, expected %d bytes, got %d\n", recvsize, n);
				printf("This is the %d error in a row out of %d tolerable errors\n", errorcount, setupInfo->maxerrors);
				if (errorcount > setupInfo->maxerrors)
				{
					printf("Maximum errors exceeded, restarting...\n\n");
					errorcount = 0;
					totalfailures++;

					if (totalfailures > setupInfo->fullrebooterrors)
						shouldexit = 1;
	
					printf("This has been total failure %d of %d...\n\n", totalfailures, setupInfo->fullrebooterrors);


					break;
				}

			}
			else // Otherwise recieve worked fine, update our texture
			{
				printf("Start memcpy: %d ms\n", SDL_GetTicks()-ticks);
				memcpy(((char*)image->pixels)+(recvsize * i), imgnetbuf, recvsize);
				errorcount = 0;
				printf("Start memcpy: %d ms\n", SDL_GetTicks()-ticks);
			}

		}
		printf("End net: %d ms\n", SDL_GetTicks()-ticks);
		/*for (int i = 0; i < 99999; i++)
			((char*)image->pixels)[i] = 'a';*/
		SDL_UnlockSurface(image);

		#ifdef _DEBUG
		printf("Finished recv, rendering...\n");
		#endif

		// Point to the recv buff
		img = (unsigned char*)image->pixels;

		// Tell the threads to run
		for (int j = 0; j < strips_online; j++)
			shouldWrite[j] = 1;
		
		// Wait for the threads to finish
		int threads_done = 0;
		while (threads_done < strips_online)
		{
			for (int j = 0; j < strips_online; j++)
				if (!shouldWrite[j])
					threads_done++;
			usleep(1);
		}


		//renderImage();
		printf("Frame took %d ms to render\n", SDL_GetTicks()-ticks);
		//usleep();
	}

	SDL_Quit();
	close(sockfd);
	free(imgnetbuf);
	free(setupInfo);
	

	if (totalfailures >= setupInfo->fullrebooterrors)
		goto start;
}
