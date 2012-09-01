#!/usr/bin/env python
import zmq, time
from util import time_us, zmq_context
from clock import ClockServer
from config import *

clock_server = ClockServer()
clock_server.start()

receiver = zmq_context.socket(zmq.REP)
receiver.bind(bind_for(COMMANDS))

broadcaster = zmq_context.socket(zmq.PUB)
broadcaster.bind(bind_for(BROADCAST))
time.sleep(1)
broadcaster.send("new song")

name, start_us = "", 0

while True:
    msg = receiver.recv_multipart()
    command = msg[0]
    if command == "play":
        if len(msg) != 3:
            receiver.send("bad format")
        else:
            name, data = msg[1:]
            with open(name, "wb") as f:
                f.write(data)
            data = None
            start_us = time_us() + 10**6 * 2
            receiver.send("ok")
            broadcaster.send("new song")

    if command == "get song":
        if len(msg) != 1:
            receiver.send("bad format")
        else:
            receiver.send_multipart(["ok", name, str(start_us)])

    if command == "get file":
        if len(msg) != 2:
            receiver.send("bad format")
        else:
            name = msg[1]
            try:
                with open(name, "rb") as f:
                    data = f.read()
            except:
                receiver.send("unknown file")
            receiver.send_multipart(["ok", data])
            data = None
