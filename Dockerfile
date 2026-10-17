FROM debian:bookworm

# Build deps for ptouch-print, PAPPL (from source), and the spike stubs, plus
# cups-ipp-utils (ipptool/ippeveprinter), usbutils (lsusb), and Pillow (fixtures).
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates \
      autoconf automake libtool gettext autopoint \
      libusb-1.0-0-dev libbluetooth-dev libgd-dev libpng-dev libjpeg-dev zlib1g-dev \
      libcups2-dev libcupsimage2-dev cups-ipp-utils usbutils \
      libssl-dev libavahi-client-dev libgnutls28-dev libkrb5-dev \
      python3 python3-pil \
 && rm -rf /var/lib/apt/lists/*

# PAPPL from source (not packaged in bookworm). Pinned to the 1.4.x line, which
# targets CUPS 2.x (bookworm's libcups2-dev 2.4); master needs libcups >= 2.5.
# Retain Debian security fixes while correcting only raster EOF reporting.
COPY patches/cups-2.4.2-deb12u9-raster.patch /tmp/cups-raster.patch
COPY scripts/build-cups.sh /tmp/build-cups.sh
RUN /tmp/build-cups.sh /tmp/cups-raster.patch
COPY patches/pappl-1.4.11-raster.patch /tmp/pappl-raster.patch
COPY patches/pappl-1.4.11-finishings.patch /tmp/pappl-finishings.patch
COPY patches/pappl-1.4.11-job-history.patch /tmp/pappl-job-history.patch
COPY patches/pappl-1.4.11-printer-operations.patch /tmp/pappl-printer-operations.patch
RUN git clone --depth 1 --branch v1.4.11 https://github.com/michaelrsweet/pappl /tmp/pappl \
 && cd /tmp/pappl && patch --batch --forward --fuzz=0 -p1 < /tmp/pappl-raster.patch \
 && patch --batch --forward --fuzz=0 -p1 < /tmp/pappl-finishings.patch \
 && patch --batch --forward --fuzz=0 -p1 < /tmp/pappl-job-history.patch \
 && patch --batch --forward --fuzz=0 -p1 < /tmp/pappl-printer-operations.patch \
 && ./configure && make && make install && ldconfig \
 && rm -rf /tmp/pappl

WORKDIR /work
