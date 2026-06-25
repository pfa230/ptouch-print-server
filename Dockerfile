FROM debian:bookworm

# Build deps for ptouch-print, PAPPL (from source), and the spike stubs, plus
# cups-ipp-utils (ipptool/ippeveprinter), usbutils (lsusb), and Pillow (fixtures).
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates autoconf \
      libusb-1.0-0-dev libgd-dev libpng-dev libjpeg-dev zlib1g-dev \
      libcups2-dev libcupsimage2-dev cups-ipp-utils usbutils \
      libssl-dev libavahi-client-dev \
      python3 python3-pil \
 && rm -rf /var/lib/apt/lists/*

# PAPPL from source (not packaged in bookworm). Pinned to the 1.4.x line, which
# targets CUPS 2.x (bookworm's libcups2-dev 2.4); master needs libcups >= 2.5.
RUN git clone --depth 1 --branch v1.4.11 https://github.com/michaelrsweet/pappl /tmp/pappl \
 && cd /tmp/pappl && ./configure && make && make install && ldconfig \
 && rm -rf /tmp/pappl

WORKDIR /work
