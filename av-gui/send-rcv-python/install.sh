#!/bin/bash

# Update package list and install required dependencies
echo "Updating package list and installing dependencies..."
sudo apt-get update && sudo apt-get install --no-recommends -y \
        python3-pip \
        python3-venv \
        python3-gi \
        python3-gi-cairo \
        python3-dev \
        gir1.2-gtk-4.0 \
        libgstreamer1.0-dev \
        libgirepository1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstreamer-plugins-bad1.0-dev \
        libffi-dev \     
        libcairo2-dev \
        libglib2.0-dev \     
        libnice10 \
        meson \
        gstreamer1.0-plugins-base \
        gstreamer1.0-nice \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        gstreamer1.0-tools \
        gstreamer1.0-x \
        gstreamer1.0-alsa \
        gstreamer1.0-gl \
        gstreamer1.0-gtk3 \
        gstreamer1.0-qt5 \
        gstreamer1.0-pulseaudio \
        build-essential \
        gcc \
        libffi-dev \
        libglib2.0-dev \
        libcairo2-dev \
        pkg-config && \
        sudo apt-get clean && \
        sudo rm -rf /var/lib/apt/lists/*

# Check and report installed dependencies
echo "Installed dependencies:"
pkg-config --list-all

# Set up the Python virtual environment
echo "Setting up Python virtual environment..."
python3 -m venv .venv

# Activate the virtual environment
source .venv/bin/activate

# Upgrade pip, setuptools, and wheel
echo "Upgrading pip, setuptools, and wheel..."
pip install --upgrade pip setuptools wheel

# Install required Python packages
echo "Installing Python packages..."
pip install --no-cache-dir websockets vext pycairo PyGObject attrs

# Inform the user to start the application manually or as needed
echo "Installation complete. Virtual environment is set up and dependencies are installed."
