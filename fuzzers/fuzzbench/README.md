# Fuzzbench

This folder contains fuzzers for benchmarking.

# Usage

Suppose you are going to make a fuzzer for `BENCHMARK`:

```sh
cd $BENCHMARK
cargo build --release --features [default,queue,setcover]
cargo make fuzzer
cargo make cfg
```
