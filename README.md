A simple tool to test, analyze and benchmark TCP connections used together with splice(2).
At the time the tool was created, I was working on:
- TSO/LRO offload implementation
- NAS type functionality under Linux/ARM, using zero copy/Samba4

Many things missing (like proper command line options and documentation...).
The server can either loopback data to the client (used for TSO/LRO validation)
or write it to disk (used to benchmark NAS functionality).
