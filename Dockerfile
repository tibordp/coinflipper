FROM ubuntu:18.04 as build

RUN apt-get update && apt-get install -y build-essential protobuf-compiler libprotobuf-dev libzmq3-dev

COPY ./Makefile /usr/src/coinflipper/Makefile
COPY ./src /usr/src/coinflipper/src
WORKDIR /usr/src/coinflipper

RUN make protobuf
RUN make

FROM ubuntu:18.04
RUN apt-get update \
    && apt-get install -y libprotobuf10 libzmq5 dumb-init \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /usr/src/coinflipper/bin/release/coinflipper /coinflipper
ENTRYPOINT ["/usr/bin/dumb-init", "--", "/coinflipper"]
CMD ["server"]