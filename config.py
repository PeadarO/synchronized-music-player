HOST = '127.0.0.1'

CLOCK = 50013
COMMANDS = 50014
BROADCAST = 50015

def address_for(port):
    return "tcp://{0}:{1}".format(HOST, port)

def bind_for(port):
    return "tcp://*:{1}".format(HOST, port)
