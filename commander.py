#!/usr/bin/env python
import zmq, sys
from util import zmq_context
from config import *

import hashlib
import os.path

def sha256(data):
    return hashlib.sha256(data).hexdigest()

socket = zmq_context.socket(zmq.REQ)
socket.connect(address_for(COMMANDS))

name = sys.argv[1]
with open(name, "rb") as f:
    data = f.read()
new_name = sha256(data)[:10] + os.path.splitext(name)[1]
socket.send_multipart(["play", new_name, data])
print socket.recv()

