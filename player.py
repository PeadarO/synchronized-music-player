#!/usr/bin/env python
import zmq, subprocess, sys, time, os
from util import time_us, zmq_context
from clock import ClockClient
from config import *

clock_client = ClockClient()
clock_client.start()

receiver = zmq_context.socket(zmq.REQ)
receiver.connect(address_for(COMMANDS))

broadcaster = zmq_context.socket(zmq.SUB)
broadcaster.connect(address_for(BROADCAST))
broadcaster.setsockopt(zmq.SUBSCRIBE, "new song")

child = None

prefix = "{0}-".format(os.getpid())

while True:
    receiver.send("get song")
    msg = receiver.recv_multipart()
    result = msg[0]
    if result != "ok":
        print result
        continue
    name, start_us = msg[1:]
    start_us = int(start_us)

    if child is not None:
        child.terminate()
        child = None

    if name == "":
        print "no more music"
    else:
        receiver.send_multipart(["get file", name])
        msg = receiver.recv_multipart()
        result = msg[0]
        if result != "ok":
            print result
            continue
        data = msg[1]

        with open(prefix + name, "wb") as f:
            f.write(data)
            f.flush()

        start_us += clock_client.delta

        child = subprocess.Popen(["./player", "--start_us={0}".format(start_us)] + sys.argv[1:] + [prefix + name])

    print broadcaster.recv()

