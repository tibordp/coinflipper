# Coin Flipper

The latest in coin-flipping technology, this program will flip any desired amount of coins at once, using a random generator of your most refined choice.

The purpose of the project is to benchmark and verify uniformness of random number generators, while providing an easy-to-use server-client model, allowing you to flip coins using thousands of CPU-saturated nodes at once. Fun for whole family!

It does so by counting occurences of coin flip streaks of any length (between 1 and 128). The client nodes generate coin flips and send their statistics to the server.

[See it in action here!](https://coinflipper.k8s.ojdip.net/)

## Building from source

With Docker:
```bash
docker build -t coinflipper .
```

Building without Docker requires Rust (1.85+) and protobuf compiler. The Docker image also includes an OCaml-based web viewer (because why not), which requires OCaml 5.2, opam, and dune.

```bash
# Build the Rust binary
cargo build --release

# Build the OCaml viewer (optional)
cd viewer && make deps && make build
```

## Usage

Coin Flipper utilises a server-client model, so both your server and your client(s) must have it compiled, and have network connectivity between each other.

What you should first set up is the server. Run `./coinflipper server` on the desired server machine. The server will attempt to load the state from `state.cf` file, if it exists. It will also periodically save the state to the same file as well as timestamped dumps in the `history/` directory.

The next thing you want to do is run `./coinflipper flipper <server address>` on each of the client nodes you wish to configure (the more the merrier!). You can optionally specify the number of worker threads with `-j <threads>` (defaults to number of CPU cores).

You can spectate the accumulated statistics on the server by running `./coinflipper status <server address>` from any machine that has network connectivity to the server.

That's it! You're done! Enjoy your coin flips!

## Running in Docker

Run server:
```bash
docker run -p 50051:50051 tibordp/coinflipper:latest
```

Run the flipper:
```bash
docker run tibordp/coinflipper:latest /coinflipper flipper <server>
```

Check the status:
```bash
docker run tibordp/coinflipper:latest /coinflipper status <server>
```

Run the web viewer:
```bash
docker run -p 8080:8080 tibordp/coinflipper:latest /viewer <server>
```

## Running on Kubernetes

Coinflipper is also Kubernetized, like any modern app ought be! You can deploy the Coinflipper server, 3 workers, and the web viewer on a cluster of your choice by

```
kubectl apply -f kubernetes/coinflipper.yaml
```

The server component will run as a StatefulSet with 1GiB of persistent storage mounted, so you will never lose your progress even if you restart the pod. The viewer is a standalone OCaml HTTP gateway that speaks gRPC to the server and serves a web UI — because a Rube Goldberg nginx-sidecar-shell-loop setup was getting too sane.

## Architecture

The Docker image ships two binaries:

- **`/coinflipper`** (Rust) — the server, flipper, and status CLI
- **`/viewer`** (OCaml) — HTTP gateway that calls GetStatus over gRPC and serves a web UI with live stats, plus a Prometheus `/metrics` endpoint

## License

Licensed under MIT (http://opensource.org/licenses/MIT), but do try to use it only for Good&trade;.
