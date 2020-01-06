#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>

#define BUFSIZE  1024


SDL_Surface *screen;
SDL_Surface *image;

void init_bmp()
{

	Uint32 rmask, gmask, bmask, amask;
	rmask = 0x0000ff00;
	gmask = 0x00ff0000;
	bmask = 0xff000000;
	amask = 0x000000ff;


	image = SDL_CreateRGBSurface(0, 640, 480, 32, rmask, gmask, bmask, amask);

	if (image == NULL)
		printf("Failed to create surface :(\n");


	SDL_Surface *bmpimage = SDL_LoadBMP("image.bmp");
	if (image == NULL)
		printf("Failed to create surface :(\n");


	image = bmpimage;


	printf("Flags: %u, %u, %u, %u\n", bmpimage->flags, bmpimage->format->BitsPerPixel, bmpimage->w, bmpimage->h);

	/* Free the allocated BMP surface */
	/*SDL_FreeSurface(image);*/
}


void renderImage()
{

	/* Blit onto the screen surface */
	if(SDL_BlitSurface(image, NULL, screen, NULL) < 0)
		fprintf(stderr, "BlitSurface error: %s\n", SDL_GetError());
	SDL_UpdateRect(screen, 0, 0, image->w, image->h);

}


struct setupData 
{
	int width;
	int height;
	int port;
	int id;
	int maxerrors;
};


struct setupData *getSetupData(char *servAddr, int port)
{
	struct setupData *dataBuf = malloc(sizeof (struct setupData));

	int sockfd;
	struct sockaddr_in servaddr, cli;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		printf("Failed to create socket\n");
		return NULL;
	}
	bzero(&servaddr, sizeof (servaddr));
	

	servaddr.sin_family = AF_INET;
	//servaddr.sin_addr.s_addr = inet_addr(servAddr);
	//servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr("192.168.88.238");
	servaddr.sin_port = htons(6969);

	printf("Connecting to server to request setup data...\n");
	printf("Server address: %s\nPort: %d\n", servAddr, port);
	if (connect(sockfd, (struct sockaddr *) &servaddr, sizeof (servaddr)) != 0)
	{
		printf("Failed to connect to the server: %s\n", strerror(errno));
		return NULL;
	}

	char req[128] = "1";
	write(sockfd, req, sizeof (req));
	read(sockfd, (char *)dataBuf, sizeof (struct setupData));


	close(sockfd);


	printf("Read data in: w: %d, h: %d\n", dataBuf->width, dataBuf->height);	
}





int main(int argc, char *argv[])
{
	int port = 6969;

	struct setupData *setupInfo = getSetupData(argv[1], port);

	return;

	srand(time(NULL));


	//socketMain(&image);
	

	/* Initialize the SDL library */
	if( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr,
				"Couldn't initialize SDL: %s\n", SDL_GetError());
		exit(1);
	}

	/* Clean up on exit */
	atexit(SDL_Quit);
	
	/*
	 * Initialize the display in a 640x480 8-bit palettized mode,
	 * requesting a software surface
	 */
	screen = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set 640x480x8 video mode: %s\n",
						SDL_GetError());
		exit(1);

	}


	/* Have a preference for 8-bit, but accept any depth */
	screen = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE|SDL_ANYFORMAT);
	if ( screen == NULL ) {
		fprintf(stderr, "Couldn't set 640x480x8 video mode: %s\n",
						SDL_GetError());
		exit(1);
	}
	printf("Set 640x480 at %d bits-per-pixel mode\n",
		   screen->format->BitsPerPixel);



	init_bmp();

	int sockfd;
	char buffer[BUFSIZE];
	char *hello = "Hello from client";
	struct sockaddr_in servaddr;

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	memset(&servaddr, 0, sizeof(servaddr));

	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = inet_addr(argv[1]);

	int n, len;

	int sendsteps = 24;


	unsigned int recvsize = (640*480*4)/sendsteps;

	sleep(1);


	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 100000; // 100ms timeout
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0)
		printf("Failed to set socket option...");


	char *imgnetbuf = malloc(recvsize+1);




	



	int errorcount = 0;
	int maxerrors = 8;
	while (1)
	{
		#ifdef _DEBUG
		printf("Sending req;\n");
		#endif
		sendto(sockfd, (const char *)hello, strlen(hello),
			0, (const struct sockaddr *) &servaddr,
				sizeof(servaddr));
		#ifdef _DEBUG
		printf("Req message sent. Waiting for reply...\n");
		#endif


		if (SDL_LockSurface(image) == -1)
			printf("Failed to lock surface for writing");
		for (int i = 0; i < sendsteps; i++)
		{
			n = recvfrom(sockfd, imgnetbuf, recvsize,
				MSG_WAITALL, (struct sockaddr *) &servaddr,
				&len);
			
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
					break;
				}

			}
			else // Otherwise recieve worked fine, update our texture
			{
				memcpy(image->pixels+(recvsize * i), imgnetbuf, recvsize);
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
		usleep(1);
	}

	close(sockfd);
}
