# Video Wall Streaming Protocol
## Windows -> *nix Implementation, PowerPC compatible

### 'nix Client
Connects to a TCP port on the server, sends MAC address over, retrieves video information based on it's position (known from lookup table on server), then recieves a port to recieve a UDP frame stream from

### Windows
- Server code modified from the Windows Desktop Duplication API example
- Grabs your screen, drops it in a buffer, opens X times Y UDP ports
- When a client connects to a port, streams a portion of the screen to it



### Notes:
You'll need to adjust sleep times on the server to meet bandwidth requirements, this will be patched out later
