# Use an Ubuntu base image
FROM ubuntu:20.04

# Set environment variables to make non-interactive installation
ENV DEBIAN_FRONTEND=noninteractive

# Update and install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    libcurl4-openssl-dev \
    libssl-dev \
    curl \
    && apt-get clean

# Copy the source code into the container
WORKDIR /app
COPY . .

# Compile the application
RUN g++ -std=c++17 -o crypto_tracker.exe main.cpp -lcurl -lcrypt32

# Expose a port if your app uses networking
# EXPOSE 8080 (uncomment and set to your app's port if needed)

# Define the entry point for the container
CMD ["./crypto_tracker.exe"]
