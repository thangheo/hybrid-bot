#!/usr/bin/env python3
import zmq
import threading
import time
import logging
import json
from typing import Callable, Dict, Any, Optional

logger = logging.getLogger(__name__)

class ZeroMQClient:
    """ZeroMQ client for publish-subscribe communication pattern"""
    
    def __init__(self, subscriber_endpoint: str, publisher_endpoint: str):
        """
        Initialize ZeroMQ client
        
        Args:
            subscriber_endpoint: Endpoint URL to subscribe to (e.g., 'tcp://localhost:5555')
            publisher_endpoint: Endpoint URL to publish to (e.g., 'tcp://localhost:5556')
        """
        self.context = zmq.Context()
        
        # Set up publisher socket
        self.publisher = self.context.socket(zmq.PUB)
        self.publisher.connect(publisher_endpoint)
        logger.info(f"Publisher connected to {publisher_endpoint}")
        
        # Set up subscriber socket
        self.subscriber = self.context.socket(zmq.SUB)
        self.subscriber.connect(subscriber_endpoint)
        logger.info(f"Subscriber connected to {subscriber_endpoint}")
        
        # Set up receiver thread
        self.running = False
        self.receive_thread = None
        self.message_handler = None
        
        # Allow time for connections to establish
        time.sleep(0.1)
    
    def subscribe(self, topic: str) -> None:
        """
        Subscribe to a topic
        
        Args:
            topic: Topic name to subscribe to
        """
        self.subscriber.setsockopt_string(zmq.SUBSCRIBE, topic)
        logger.info(f"Subscribed to topic: {topic}")
    
    def unsubscribe(self, topic: str) -> None:
        """
        Unsubscribe from a topic
        
        Args:
            topic: Topic name to unsubscribe from
        """
        self.subscriber.setsockopt_string(zmq.UNSUBSCRIBE, topic)
        logger.info(f"Unsubscribed from topic: {topic}")
    
    def publish(self, topic: str, message: Dict[str, Any]) -> bool:
        """
        Publish a message to a topic
        
        Args:
            topic: Topic name to publish to
            message: Message dictionary to publish (will be converted to JSON)
            
        Returns:
            True if successful, False otherwise
        """
        try:
            message_json = json.dumps(message)
            self.publisher.send_string(topic, flags=zmq.SNDMORE)
            self.publisher.send_string(message_json)
            return True
        except Exception as e:
            logger.error(f"Failed to publish message: {str(e)}")
            return False
    
    def publish_raw(self, topic: str, message: str) -> bool:
        """
        Publish a raw string message to a topic
        
        Args:
            topic: Topic name to publish to
            message: Raw message string to publish
            
        Returns:
            True if successful, False otherwise
        """
        try:
            self.publisher.send_string(topic, flags=zmq.SNDMORE)
            self.publisher.send_string(message)
            return True
        except Exception as e:
            logger.error(f"Failed to publish raw message: {str(e)}")
            return False
    
    def set_message_handler(self, handler: Callable[[str, Dict[str, Any]], None]) -> None:
        """
        Set handler for received messages
        
        Args:
            handler: Function to call when a message is received (receives topic and message dict)
        """
        self.message_handler = handler
    
    def start(self) -> None:
        """Start receiving messages in a background thread"""
        if self.running:
            return
        
        self.running = True
        self.receive_thread = threading.Thread(target=self._receive_worker)
        self.receive_thread.daemon = True
        self.receive_thread.start()
        logger.info("ZeroMQ client started")
    
    def stop(self) -> None:
        """Stop receiving messages"""
        if not self.running:
            return
        
        self.running = False
        if self.receive_thread:
            self.receive_thread.join(1.0)  # Wait for 1 second
            self.receive_thread = None
        
        logger.info("ZeroMQ client stopped")
    
    def _receive_worker(self) -> None:
        """Worker thread for receiving messages"""
        poller = zmq.Poller()
        poller.register(self.subscriber, zmq.POLLIN)
        
        while self.running:
            try:
                socks = dict(poller.poll(100))  # Poll with 100ms timeout
                
                if self.subscriber in socks and socks[self.subscriber] == zmq.POLLIN:
                    topic = self.subscriber.recv_string()
                    message = self.subscriber.recv_string()
                    
                    if self.message_handler:
                        try:
                            message_dict = json.loads(message)
                            self.message_handler(topic, message_dict)
                        except json.JSONDecodeError:
                            # Handle raw message if not valid JSON
                            self.message_handler(topic, {"raw_message": message})
            except Exception as e:
                logger.error(f"Error in receive worker: {str(e)}")
                time.sleep(0.1)  # Small sleep to prevent CPU spinning
