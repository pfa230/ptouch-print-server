#!/usr/bin/env python3
# Throwaway socket sink for the M0 spike: PAPPL's socket:// device connects here;
# we just drain bytes to a file so the raster callbacks fire (Probe B1 logging).
import socket
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 9100))
srv.listen(5)
print("sink listening on 127.0.0.1:9100", flush=True)
while True:
    conn, _ = srv.accept()
    with open("/tmp/out.prn", "wb") as f:
        while True:
            d = conn.recv(65536)
            if not d:
                break
            f.write(d)
    conn.close()
