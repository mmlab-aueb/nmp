FROM ubuntu:latest

# Set environment variables to avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Update package list and install GStreamer libraries and plugins
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
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
        apt-get clean && \
        rm -rf /var/lib/apt/lists/*


RUN python3 -m venv /opt/venv 

ENV PATH="/opt/venv/bin:$PATH"
ENV XDG_RUNTIME_DIR=/tmp/xdg
RUN mkdir -p /tmp/xdg


#RUN /opt/venv/bin/pip3 install websockets
#RUN /opt/venv/bin/pip3 install vext
#RUN /opt/venv/bin/pip3 install PyGObject
#RUN /opt/venv/bin/pip3 install pycairo
RUN /opt/venv/bin/pip3 install \
    websockets \
    vext \
    PyGObject \ 
    pycairo \
    attrs 


# Set the working directory in the container
WORKDIR /app

# Copy init.py and caller.py files into the container
COPY init.py /app/init.py
COPY caller.py /app/caller.py

RUN echo "source /opt/venv/bin/activate" >> ~/.bashrc

EXPOSE 10000


#CMD [ "python", "init.py", "--server", "wss://198.:10000" ]





