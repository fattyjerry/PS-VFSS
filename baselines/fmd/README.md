# fuzzycrypto

⚠️ **DO NOT USE THIS LIBRARY IN PRODUCTION LEVEL CODE** ⚠️

## Fuzzy Message Detection Schemes 
This repository contains research code for benchmarking the FMD2 and FracFMD schemes as described in [Fuzzy Message Detection](https://eprint.iacr.org/2021/089.pdf). This paper introduces a new cryptographic primitive known as a _fuzzy message detection scheme_ (FMD). FMD is a tool that facilitates receiver anonymity in store-and-forward messaging systems where a single party or small collection of parties aggregates messages for a large number of users. Using the key generation algorithm of the FMD scheme, a receiver can publish a public key to allow senders to address a message to them. At a later point in time the receiver can then outsource detection of messages to a third party by extracting a "faulty" key from their secret key and handing it off to the server. The server then uses this key to test if a message was meant for the receiver. The faulty key (called a _detection key_ in the paper) has associated with it some false positive rate _p_ so that each message not intended for the receiver has some chance of being forward to the receiver. FMD security requires that to the server a true and false positive are indistinguishable from one another. 


## Notes on Repo

The interface for FMD is defined in _scheme.go_. The package _toygarble_ contains code to garble a circuit provided in Bristol format. The directory _c2c-converter_ contains files related to the CBMCGCC compiler which can take in C programs and output Boolean circuits. They also provide the ability to output files in Bristol (which we make use of). 

      

It should go without saying that as research code this is untested, does not necessarily have any protection against side channel attacks, does not handle errors gracefully, is unoptimized, has copy pasta, etc, etc. 

Other implementations of FMD2 in particular can be found at the following locations:

- https://crates.io/crates/fuzzytags [Rust]
- https://github.com/gtank/gophertags [Go - WIP according to README]


----


To run the benchmarks:

go test -bench=.

For FMD2 the benchmarks specify a 24 bit ciphertext, and perform extraction/testing at N=5, N=10, N=15. These parameters can be changed by changing the relevant test files.

