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

#define SANITY_CHECK_INT 6969
#define SANITY_CHECK_CHAR 'a'

int tcpport = 6969;
char *ip = "192.168.88.238";

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

struct setupData *setupInfo;


SDL_Surface *screen;
SDL_Surface *image;

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
				name = malloc(strlen(ifa->ifa_name)+1);
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
	
	char *macout = malloc(32);
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
	struct setupData *dataBuf = malloc(sizeof (struct setupData));

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





int main(int argc, char *argv[])
{

start:

	while ((setupInfo = getSetupData(ip, tcpport)) == NULL); // Keep trying to get setup data until it works

	//return;

	srand(time(NULL));
	

	/* Initialize the SDL library */
	if( SDL_Init(SDL_INIT_VIDEO) < 0 ) 
	{
		fprintf(stderr,	"Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

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
		exit(1);
	}
	printf("Set 640x480 at %d bits-per-pixel mode\n",
		   screen->format->BitsPerPixel);



	init_screen_texture();
	printf("Initialized SDL!\n");
	fflush(stdout);

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
	tv.tv_usec = 300000; // 300ms timeout
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0)
		printf("Failed to set socket option...\n\n");


	char *imgnetbuf = malloc(recvsize+1);


	#define _DEBUG 1



	SDL_Event event;
	
	int errorcount = 0;
	int maxerrors = 8;
	int shouldexit = 0;
	int totalfailures = 0;
	Uint32 ticks;
	while (1)
	{
		ticks = SDL_GetTicks();
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
		if (shouldexit)
			break;


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
			n = recvfrom(sockfd, imgnetbuf, recvsize,
				MSG_WAITALL, (struct sockaddr *) &servaddr,
				&len);
			
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
				printf("This is the %d error in a row out of %d tolerable errors\n", errorcount, maxerrors);
				if (errorcount > maxerrors)
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
				memcpy(((char*)image->pixels)+(recvsize * i), imgnetbuf, recvsize);
				errorcount = 0;
			}

		}
		/*for (int i = 0; i < 99999; i++)
			((char*)image->pixels)[i] = 'a';*/
		SDL_UnlockSurface(image);

		#ifdef _DEBUG
		printf("Finished recv, rendering...\n");
		#endif

		renderImage();
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

