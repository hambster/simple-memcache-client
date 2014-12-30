simple-memcache-client
======================
No internal/builtin hashing algorithm was included. This project provides a simple memcached client library implemented by C with TCP/IP.

The major purpose of this project is providing a basic building blocks for client application without reinventing wheel. It's the responsibility of client application to decide how to partition and manage the connection between various memcached server. 
