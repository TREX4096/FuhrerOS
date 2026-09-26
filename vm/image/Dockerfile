# FuhrerOS image builder.
#
# Stage 1 compiles and unit-tests the adaptive layer against the same libc
# (musl) and liburing that ship in the guest. Stage 2 is the toolbox that
# assembles the root filesystem (vm/image/mkimage.sh), run by scripts/build.sh
# with the output directory bind-mounted at /out.
ARG ALPINE=3.22

FROM alpine:${ALPINE} AS build
RUN apk add --no-cache build-base linux-headers liburing-dev
WORKDIR /src
COPY mk ./mk
COPY adaptive ./adaptive
COPY runtime ./runtime
COPY benchmarks ./benchmarks
COPY tests/unit ./tests/unit
RUN make -f mk/native.mk -j"$(nproc)" all \
 && make -f mk/native.mk test \
 && make -f mk/native.mk install DESTDIR=/dist

FROM alpine:${ALPINE} AS imager
ARG ALPINE
ENV ALPINE_BRANCH=v${ALPINE}
RUN apk add --no-cache e2fsprogs qemu-img mkinitfs openssh-keygen xz tar bash
COPY --from=build /dist /dist
COPY vm/image /image
RUN chmod +x /image/mkimage.sh
CMD ["/image/mkimage.sh"]
