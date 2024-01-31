#!/bin/bash

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed. Please install Docker and try again."
    exit 1
fi

# Check if the Docker image is already pulled
DOCKER_IMAGE="ghcr.io/carlosperate/microbit-toolchain:latest"

if [[ "$(docker images -q $DOCKER_IMAGE 2> /dev/null)" == "" ]]; then
    echo "Pulling Docker image: $DOCKER_IMAGE"
    docker pull $DOCKER_IMAGE
fi

# Run the Docker command
docker run -v ${PWD}:/home --rm $DOCKER_IMAGE "$@"