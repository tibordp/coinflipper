FROM ubuntu:latest as build

RUN apt update && apt-get install -y build-essential protobuf-compiler libprotobuf-dev libzmq3-dev

COPY ./Makefile /usr/src/coinflipper/Makefile
COPY ./src /usr/src/coinflipper/src
WORKDIR /usr/src/coinflipper

RUN make protobuf
RUN make

FROM ubuntu:latest
RUN apt update && apt-get install -y libprotobuf10 libzmq5
COPY --from=build /usr/src/coinflipper/bin/release/coinflipper /coinflipper
ENTRYPOINT ["/coinflipper"]
CMD ["server"]