FROM --platform=linux/arm64 ubuntu:26.04 AS builder

RUN export DEBIAN_FRONTEND=noninteractive \
 && apt-get -qq update \
 && apt-get -qq upgrade

RUN export DEBIAN_FRONTEND=noninteractive \
 && apt-get install --no-install-recommends -y \
        cmake \
        g++ \
        make \
        pkg-config \
        python3-dev \
        devscripts \
        equivs \
        sudo \
        xterm \
        apt-utils \
        git \
        autotools-dev \
        autoconf \
        libtool \
        intltool \
        debhelper \
        build-essential \
        libglib2.0-dev \
        libgtk-3-dev \
        libgtk-4-dev \
        libgtk2.0-dev \
        qtbase5-dev \
        qtbase5-private-dev \
        qt6-base-dev \
        qt6-base-private-dev \
        qt6-tools-dev \
        qt6-tools-dev-tools \
        libwayland-dev \
        wayland-protocols \
        libxkbcommon-dev \
        libhangul-dev \
        libhangul-data \
        libanthy-dev \
        anthy \
        librime-dev \
        libm17n-dev \
        m17n-db \
        libxklavier-dev \
        libayatana-appindicator3-dev \
        librsvg2-bin \
        fonts-noto-cjk \
        gtk-doc-tools \
        lsb-release

COPY . /src
WORKDIR /src

RUN yes | mk-build-deps --install --root-cmd sudo --remove /src/debian/control

ENV DEB_BUILD_ARCH=arm64
ENV DEB_HOST_ARCH=arm64

RUN autoreconf --force --install --verbose \
 && ./configure --prefix=/usr \
 && make -j $(nproc)

ENTRYPOINT ["bash"]
