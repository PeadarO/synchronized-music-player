import time, zmq
    
def average_middle(values):
    values = list(sorted(values))
    num = len(values)
    if num == 0:
        return 0
    else:
        subset = values[num/4:num/4+(num+1)/2]
        return sum(subset) / len(subset)

def time_us():
    return int(time.time() * 10**6)

zmq_context = zmq.Context()

