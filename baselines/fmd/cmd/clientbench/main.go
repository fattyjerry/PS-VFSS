package main

import (
	"crypto/elliptic"
	"crypto/rand"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"time"

	fuzzy "github.com/becgabri/fuzzycrypto"
)

type output struct {
	Scheme                string `json:"scheme"`
	Ns                    int    `json:"Ns"`
	Nr                    int    `json:"Nr"`
	ActualK               int    `json:"actual_k"`
	ObservedResultCount   int    `json:"observed_result_count"`
	FalsePositiveCount    int    `json:"false_positive_count"`
	SignalingGenerationNS int64  `json:"sender_signaling_generation_ns"`
	SerializationNS       int64  `json:"sender_serialization_ns"`
	SenderTotalOnlineNS   int64  `json:"sender_total_online_ns"`
	RecipientProcessingNS int64  `json:"recipient_processing_ns"`
	Correctness           bool   `json:"correctness"`
	OutputType            string `json:"output_type"`
	ErrorClass            string `json:"error_class"`
	SchemeSpecificParams  string `json:"scheme_specific_parameters"`
}

func main() {
	n := flag.Int("Ns", 16, "number of independent flags")
	k := flag.Int("k", 1, "number of true matches")
	gamma := flag.Int("gamma", 8, "FMD false-positive exponent")
	flag.Parse()

	r := output{
		Scheme: "FMD", Ns: *n, Nr: 2, ActualK: *k,
		OutputType:           "candidate_indices",
		SchemeSpecificParams: fmt.Sprintf("gamma=%d;p=2^-%d;curve=P-256", *gamma, *gamma),
	}
	if *n <= 0 || *k < 0 || *k > *n || *gamma <= 0 {
		r.ErrorClass = "invalid_parameters"
		emit(r)
		os.Exit(2)
	}

	curve := elliptic.P256()
	var scheme fuzzy.ElGamalPower2
	sk, pk := scheme.KeyGen(curve, *gamma, rand.Reader)
	_, decoyPK := scheme.KeyGen(curve, *gamma, rand.Reader)
	dsk := scheme.Extract(*gamma, sk)
	if sk == nil || pk == nil || decoyPK == nil || dsk == nil {
		r.ErrorClass = "setup_failed"
		emit(r)
		os.Exit(1)
	}

	flags := make([][]byte, *n)
	startTotal := time.Now()
	start := time.Now()
	for i := 0; i < *n; i++ {
		if i < *k {
			flags[i] = scheme.Flag(curve, rand.Reader, pk)
		} else {
			flags[i] = scheme.Flag(curve, rand.Reader, decoyPK)
		}
	}
	r.SignalingGenerationNS = time.Since(start).Nanoseconds()

	// Flag already returns the protocol byte encoding. Copying it into the
	// outbound buffers is the sender serialization boundary; no network I/O is timed.
	start = time.Now()
	serialized := make([][]byte, len(flags))
	for i := range flags {
		serialized[i] = append([]byte(nil), flags[i]...)
	}
	r.SerializationNS = time.Since(start).Nanoseconds()
	r.SenderTotalOnlineNS = time.Since(startTotal).Nanoseconds()

	candidates := make([]int, 0, *k)
	for i, encoded := range serialized {
		if scheme.Test(curve, encoded, dsk) {
			candidates = append(candidates, i)
		}
	}
	r.ObservedResultCount = len(candidates)
	r.FalsePositiveCount = len(candidates) - *k

	start = time.Now()
	seen := make([]bool, *k)
	for _, idx := range candidates {
		if idx >= 0 && idx < *k {
			seen[idx] = true
		}
	}
	allTruePresent := true
	for _, present := range seen {
		allTruePresent = allTruePresent && present
	}
	r.RecipientProcessingNS = time.Since(start).Nanoseconds()
	r.Correctness = allTruePresent
	if !r.Correctness {
		r.ErrorClass = "true_match_missing"
	}
	emit(r)
	if !r.Correctness {
		os.Exit(3)
	}
}

func emit(r output) {
	encoded, err := json.Marshal(r)
	if err != nil {
		panic(err)
	}
	fmt.Printf("[CLIENT_BENCH_JSON] %s\n", encoded)
}
