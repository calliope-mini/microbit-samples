@echo off

:: Check if Docker is installed
where docker >nul 2>nul
if %errorlevel% neq 0 (
    echo Error: Docker is not installed. Please install Docker and try again.
    exit /b 1
)

:: Check if the Docker image is already pulled
set DockerImage=ghcr.io/carlosperate/microbit-toolchain:latest
docker images %DockerImage% | findstr /i %DockerImage% >nul 2>nul
if %errorlevel% neq 0 (
    echo Pulling Docker image: %DockerImage%
    docker pull %DockerImage%
)

:: Run the Docker command
docker run -v %CD%:/home --rm %DockerImage% %*