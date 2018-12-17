# epump
This is a C-language library based on non-blocking communication and multi-threaded event-driven model, which can help you develop server with high-performance and numerous concurrent connections.

Many server programs must handle a large quantity of TCP connections from client sides, such as Web server, online server, message server, etc. In earlier implementation of communication server, a connection request is usually received and processed by one standalone process or thread, the typical application is the eralier version of apahce web server. Based on asynchromous readiness notification of file descriptor, the process or thread can be unnecessary to block till the coming of data. By setting the file descriptors as non-blocking and monitoring the state of fd-sets read or write with epoll/select facilities, the epump library implementation creates IO-device objects for the management of fds, adds IO-timer facility for the timing-driven requirements. Fully utlizing the capacity by starting the same number of threads as CPU cores, the epump library adopts the callback mechanism for application developer.

Lots of complicated underlying details are encapsulated and easy APIs are provided for fast development of high performance server program.
