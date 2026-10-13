#!/bin/sh
# Rebuild only ABI-compatible libraries from Debian's security-patched source.
# Both images call this helper before compiling PAPPL.
set -eu
cups_source_version=2.4.2-3+deb12u9
cups_patch_file=${1:?usage: build-cups.sh PATCH}
case "$cups_patch_file" in /*) ;; *) echo 'CUPS patch path must be absolute' >&2; exit 1 ;; esac
sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/debian.sources
apt-get update
installed_version=$(dpkg-query -W -f='${source:Version}' libcups2)
available_version=$(apt-cache show --no-all-versions libcups2 | sed -n 's/^Version: //p')
if test "$installed_version" != "$cups_source_version" || test "$available_version" != "$cups_source_version"; then
  echo "Refresh the pinned CUPS patch: installed=$installed_version available=$available_version expected=$cups_source_version" >&2
  exit 1
fi
mkdir -p /tmp/ptouch-cups-build
cd /tmp/ptouch-cups-build
apt-get source "cups=$cups_source_version"
cd cups-2.4.2
patch --batch --forward --fuzz=0 -p1 < "$cups_patch_file"
# Retain Debian's library TLS, DNS-SD, Kerberos, and hardening configuration.
export DEB_BUILD_MAINT_OPTIONS=hardening=+all,-pie
export CFLAGS="$(dpkg-buildflags --get CFLAGS)"
export CPPFLAGS="$(dpkg-buildflags --get CPPFLAGS)"
export LDFLAGS="$(dpkg-buildflags --get LDFLAGS)"
export DSOFLAGS="$LDFLAGS"
./configure --prefix=/usr/local --libdir=/usr/local/lib \
  --with-tls=gnutls --with-dnssd=avahi --enable-gssapi --enable-threads
make -C cups libs
make -C cups install-libs
mkdir -p /usr/local/share/ptouch
printf '%s\n' "$cups_source_version" > /usr/local/share/ptouch/cups-source-version
ldconfig
# No scheduler, filters, clients, or new headers are installed.
cd /tmp
rm -r /tmp/ptouch-cups-build
find /var/lib/apt/lists -type f -delete
