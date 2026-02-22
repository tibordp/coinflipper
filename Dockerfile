# Stage 1: Build Rust binary
FROM rust:1.93 AS rust-build
WORKDIR /usr/src/coinflipper
RUN apt-get update && apt-get install -y protobuf-compiler
COPY Cargo.toml Cargo.lock ./
COPY src/ src/
COPY proto/ proto/
COPY build.rs .
RUN cargo build --release

# Stage 2: Build OCaml viewer binary
FROM ocaml/opam:debian-13-ocaml-5.2 AS ocaml-build
RUN sudo apt-get update && sudo apt-get install -y protobuf-compiler pkg-config libgmp-dev
WORKDIR /home/opam/viewer
COPY --chown=opam:opam viewer/dune-project viewer/viewer.opam ./
RUN opam install . --deps-only --yes
COPY --chown=opam:opam viewer/ .
RUN opam exec -- dune build

# Stage 3: Final image with both binaries
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=rust-build /usr/src/coinflipper/target/release/coinflipper /coinflipper
COPY --from=ocaml-build /home/opam/viewer/_build/default/bin/main.exe /viewer
COPY --from=ocaml-build /home/opam/viewer/static/ /viewer-static/
CMD ["/coinflipper", "server"]
