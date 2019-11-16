# Coin Flipper

The latest in coin-flipping technology, this program will flip any desired amount of coins at once, using a random generator of your most refined choice.

The purpose of the project is to benchmark and verify uniformness of random number generators, while providing an easy-to-use server-client model, allowing you to flip coins using thousands of CPU-saturated nodes at once. Fun for whole family!

It does so by counting occurences of coin flip streaks of any length (between 1 and 128). The client nodes generate coin flips and send their statistics to the server.

## Building from source

With Docker:
```bash
docker build -t coinflipper .
```

Building without Docker requires an implementation of Google's Protocol Buffers compiler (protobuf), ZeroMQ, GNU Make, a C++11-able C++ compiler (e.g. GCC >=4.7.0, Clang), and a sensible build environment.

Run `make protobuf`, then `make`. It's that easy!

## Usage

Coin Flipper utilises a server-client model, so both your server and your client(s) must have it compiled, and have network connectivity between each other.

What you should first set up is the server. Run `./coinflipper server` on the desired server machine. The server will attempt to load the state from `state.cf` file, if it exists. It will also periodically save the state to the same file as well as timestamped dumps in the `history/` directory.

The next thing you want to do is run `./coinflipper flipper <server address>` on each of the client nodes you wish to configure (the more the merrier!).

You can spectate the accumulated statistics on the server by running `./coinflipper status <server address>` from any machine that has network connectivity to the server.

That's it! You're done! Enjoy your coin flips!

## Running in Docker

Run server:
```bash
docker run -p 5555:5555 -p 5556:5556 tibordp/coinflipper:latest
```

Run the flipper:
```bash
docker run tibordp/coinflipper:latest flipper <server>
```

Check the status:
```bash
docker run tibordp/coinflipper:latest status <server>
```

## License

Licensed under MIT (http://opensource.org/licenses/MIT), but do try to use it only for Good&trade;.
