#!/usr/bin/env python3
import zmq
import time
import logging
import sys

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    stream=sys.stdout
)
logger = logging.getLogger("zmq_proxy")

def main():
    try:
        context = zmq.Context()
        
        # Socket facing publishers
        frontend = context.socket(zmq.XSUB)
        frontend.bind("tcp://*:5556")
        
        # Socket facing subscribers
        backend = context.socket(zmq.XPUB)
        backend.bind("tcp://*:5555")
        
        logger.info("ZeroMQ proxy started")
        logger.info("XPUB listening on tcp://*:5555")
        logger.info("XSUB listening on tcp://*:5556")
        
        # Start the proxy
        zmq.proxy(frontend, backend)
    except Exception as e:
        logger.error(f"Error in ZeroMQ proxy: {str(e)}")
        time.sleep(1)  # Prevent rapid restarts if there's an issue

if __name__ == "__main__":
    main()
