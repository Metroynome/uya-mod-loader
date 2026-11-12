FROM dnawrkshp/ps2dev-libdl:latest

# Pin the library revisions used to validate the loader; override to upgrade.
ARG LIBRAC3_REF=f94863cf16b7d41624ed46655e9d55f67b115c3b
ARG LIBUYA_REF=20e271da78a37974f354e0dca49dd1fbd60b2c20
ENV LIBRAC3_DIR=/opt/uya-mod-loader-deps/librac3 \
    LIBUYA_DIR=/opt/uya-mod-loader-deps/horizon-uya-patch/libuya
COPY docker-init.sh /usr/local/bin/uya-docker-init
RUN sh /usr/local/bin/uya-docker-init

WORKDIR /workspace
# Mount the repository here so generated modules and release files stay on host.
CMD ["make", "all"]
