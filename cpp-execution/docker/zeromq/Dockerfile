FROM debian:bullseye-slim

RUN apt-get update && apt-get install -y \
    build-essential \
    libzmq3-dev \
    pkg-config \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install pyzmq

COPY zmq_proxy.py /usr/local/bin/zmq_proxy.py

CMD ["python3", "/usr/local/bin/zmq_proxy.py"]