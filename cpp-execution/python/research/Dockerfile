FROM python:3.9

WORKDIR /app

# Install dependencies
RUN pip install zmq numpy pandas matplotlib seaborn

# Copy the script
COPY order_book_analyzer.py /app/

# Make it executable
RUN chmod +x /app/order_book_analyzer.py

# Set entrypoint
ENTRYPOINT ["python", "/app/order_book_analyzer.py"]
