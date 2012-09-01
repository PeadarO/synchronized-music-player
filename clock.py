import zmq, time, threading, collections, struct
from util import time_us, zmq_context, average_middle
from config import *

TIME_FORMAT = "q"
TIME_STRUCT = struct.Struct(TIME_FORMAT)

class ClockServer(threading.Thread):
    def __init__(self):
        super(ClockServer, self).__init__()
        self.daemon = True
        self.socket = zmq_context.socket(zmq.REP)
        self.socket.bind(bind_for(CLOCK))

    def run(self):
        while True:
            message = self.socket.recv()
            now = time_us()
            self.socket.send(TIME_STRUCT.pack(now))

class ClockClient(threading.Thread):
    def __init__(self):
        super(ClockClient, self).__init__()
        self.daemon = True
        self.socket = zmq_context.socket(zmq.REQ)
        self.socket.connect(address_for(CLOCK))
        self.deltas = collections.deque(maxlen=100)

    def run(self):
        while True:
            for i in range(5):
                before = time_us()
                self.socket.send("")
                during, = TIME_STRUCT.unpack(self.socket.recv())
                after = time_us()
                self.deltas.append((after + before) / 2 - during)
            time.sleep(5)

    @property
    def delta(self):
        return average_middle(self.deltas)

