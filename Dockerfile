FROM dnawrkshp/ps2dev-libdl:latest

# Pin the library revisions used to validate the loader; override to upgrade.
ARG LIBRAC3_REF=b14f8259e6072d8a770d86b08895ad33fb398dc8
ARG LIBUYA_REF=737dd562c7ea142de3e708594f74c239f19af4b0
ENV LIBRAC3_DIR=/opt/uya-mod-loader-deps/librac3 \
    LIBUYA_DIR=/opt/uya-mod-loader-deps/horizon-uya-patch/libuya
COPY docker-init.sh /usr/local/bin/uya-docker-init
RUN sh /usr/local/bin/uya-docker-init

WORKDIR /workspace
# Mount the repository here so generated modules and release files stay on host.
CMD ["make", "all"]
