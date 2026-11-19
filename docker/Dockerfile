# The Grav compiler in an image, on Debian or Ubuntu:
#
#   docker build -f docker/Dockerfile -t grav:debian .
#   docker build -f docker/Dockerfile -t grav:ubuntu --build-arg BASE=ubuntu:24.04 .
#   docker run --rm -v "$PWD":/app grav:debian run hello.grav
#
# The first stage builds grav with install.sh; the second has only what using
# grav needs (a C compiler and the optional libraries) plus the installed files.
ARG BASE=debian:bookworm-slim

FROM ${BASE} AS build
COPY . /src
RUN /src/install.sh --yes --prefix /usr/local --no-path --no-optional

FROM ${BASE}
COPY install.sh /tmp/install.sh
RUN /tmp/install.sh --deps-only --yes --optional \
    && rm -rf /tmp/install.sh /var/lib/apt/lists/*
COPY --from=build /usr/local/bin/grav /usr/local/bin/grav
COPY --from=build /usr/local/share/grav/lib /usr/local/share/grav/lib
ENV GRAV_PACKAGES=/root/.local/grav/packages
WORKDIR /app
ENTRYPOINT ["grav"]
CMD ["--help"]
