package main

import (
	"bytes"
	"crypto/elliptic"
	"crypto/rand"
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"time"

	fuzzy "github.com/becgabri/fuzzycrypto"
)

type result struct {
	scheme         string
	n              int
	ell            int
	setupMS        int64
	sendMS         int64
	serverMS       int64
	serializeUS    int64
	recipientMS    int64
	recipientConsumeNS float64
	recipientInnerOps int
	recipientConsumeOK bool
	commBytes      int
	candidates     int
	trueHits       int
	falsePositives int
	status         string
	bottleneck     string
}

func elapsedMS(start time.Time) int64 {
	return time.Since(start).Round(time.Millisecond).Milliseconds()
}

func slowest(r result) string {
	stage := "setup"
	best := r.setupMS
	if r.sendMS > best {
		stage = "send"
		best = r.sendMS
	}
	if r.serverMS > best {
		stage = "server"
		best = r.serverMS
	}
	if r.recipientMS > best {
		stage = "recipient"
	}
	return stage
}

func run(n, ell, gamma int) result {
	r := result{
		scheme: "FMD",
		n:      n,
		ell:    ell,
		status: "completed",
	}
	if n <= 0 || ell < 0 || ell > n || gamma <= 0 {
		r.status = "crashed"
		r.bottleneck = "invalid_parameters"
		return r
	}

	curve := elliptic.P256()
	var scheme fuzzy.ElGamalPower2

	start := time.Now()
	sk, pk := scheme.KeyGen(curve, gamma, rand.Reader)
	_, decoyPK := scheme.KeyGen(curve, gamma, rand.Reader)
	r.setupMS = elapsedMS(start)
	if sk == nil || pk == nil || decoyPK == nil {
		r.status = "crashed"
		r.bottleneck = "keygen_failed"
		return r
	}

	start = time.Now()
	dsk := scheme.Extract(gamma, sk)
	r.recipientMS = elapsedMS(start)
	if dsk == nil {
		r.status = "crashed"
		r.bottleneck = "extract_failed"
		return r
	}

	messages := make([][]byte, 0, n)
	start = time.Now()
	for i := 0; i < n; i++ {
		if i < ell {
			messages = append(messages, scheme.Flag(curve, rand.Reader, pk))
		} else {
			messages = append(messages, scheme.Flag(curve, rand.Reader, decoyPK))
		}
	}
	r.sendMS = elapsedMS(start)

	candidates := make([]uint64, 0, ell)
	start = time.Now()
	for i, msg := range messages {
		if scheme.Test(curve, msg, dsk) {
			candidates = append(candidates, uint64(i+1))
		}
	}
	r.serverMS = time.Since(start).Microseconds()

	start = time.Now()
	trueHits := 0
	for _, location := range candidates {
		if location <= uint64(ell) {
			trueHits++
		}
	}
	r.recipientMS += elapsedMS(start)
	serializeStart := time.Now()
	var response bytes.Buffer
	_ = binary.Write(&response, binary.LittleEndian, uint32(len(candidates)))
	for _, location := range candidates {
		_ = binary.Write(&response, binary.LittleEndian, location)
	}
	r.serializeUS = time.Since(serializeStart).Microseconds()
	r.commBytes = response.Len()
	const recipientInnerOps = 1000
	responseBytes := response.Bytes()
	var decoded []uint64
	recipientStart := time.Now()
	for op := 0; op < recipientInnerOps; op++ {
		reader := bytes.NewReader(responseBytes)
		var count uint32
		if err := binary.Read(reader, binary.LittleEndian, &count); err != nil {
			r.status = "crashed"
			r.bottleneck = "recipient_count_decode_failed"
			return r
		}
		decoded = make([]uint64, count)
		if err := binary.Read(reader, binary.LittleEndian, &decoded); err != nil || reader.Len() != 0 {
			r.status = "crashed"
			r.bottleneck = "recipient_location_decode_failed"
			return r
		}
	}
	r.recipientConsumeNS = float64(time.Since(recipientStart).Nanoseconds()) / recipientInnerOps
	r.recipientInnerOps = recipientInnerOps
	r.recipientConsumeOK = len(decoded) == len(candidates)
	if r.recipientConsumeOK {
		for i := range decoded {
			if decoded[i] != candidates[i] {
				r.recipientConsumeOK = false
				break
			}
		}
	}
	r.candidates = len(candidates)
	r.trueHits = trueHits
	r.falsePositives = len(candidates) - trueHits

	if trueHits != ell {
		r.status = "crashed"
		r.bottleneck = fmt.Sprintf("true_positive_mismatch_gamma=%d_p=2^-%d_candidates=%d", gamma, gamma, len(candidates))
	} else {
		r.bottleneck = fmt.Sprintf("%s_gamma=%d_p=2^-%d_candidates=%d", slowest(r), gamma, gamma, len(candidates))
	}
	return r
}

func average(n, ell, gamma, reps int) result {
	avg := result{
		scheme: "FMD",
		n:      n,
		ell:    ell,
		status: "completed",
	}
	var lastBottleneck string
	for i := 0; i < reps; i++ {
		r := run(n, ell, gamma)
		avg.setupMS += r.setupMS
		avg.sendMS += r.sendMS
		avg.serverMS += r.serverMS
		avg.recipientMS += r.recipientMS
		avg.recipientConsumeNS += r.recipientConsumeNS
		avg.commBytes += r.commBytes
		avg.serializeUS += r.serializeUS
		avg.candidates += r.candidates
		avg.trueHits += r.trueHits
		avg.falsePositives += r.falsePositives
		if r.status != "completed" {
			avg.status = r.status
		}
		avg.recipientConsumeOK = avg.recipientConsumeOK || i == 0
		avg.recipientConsumeOK = avg.recipientConsumeOK && r.recipientConsumeOK
		avg.recipientInnerOps = r.recipientInnerOps
		lastBottleneck = r.bottleneck
	}
	avg.setupMS /= int64(reps)
	avg.sendMS /= int64(reps)
	avg.serverMS /= int64(reps)
	avg.recipientMS /= int64(reps)
	avg.recipientConsumeNS /= float64(reps)
	avg.commBytes /= reps
	avg.serializeUS /= int64(reps)
	avg.candidates /= reps
	avg.trueHits /= reps
	avg.falsePositives /= reps
	avg.bottleneck = fmt.Sprintf("%s_avg_reps=%d", lastBottleneck, reps)
	return avg
}

func printResult(r result) {
	fmt.Printf("[BENCH_CSV] %s,%d,%d,%d,%d,%d,%d,%d,%s,%s\n",
		r.scheme,
		r.n,
		r.ell,
		r.setupMS,
		r.sendMS,
		r.serverMS,
		r.recipientMS,
		r.commBytes,
		r.status,
		r.bottleneck,
	)
	fmt.Printf("[PILOT_JSON] {\"scheme\":\"FMD\",\"N\":%d,\"k_actual\":%d,\"admission_status\":\"unsupported\",\"admission_online_ms\":null,\"retrieval_core_ms\":%.6f,\"response_serialization_ms\":%.6f,\"retrieval_online_ms\":%.6f,\"server0_response_bytes\":%d,\"server1_response_bytes\":null,\"server_to_recipient_bytes\":%d,\"candidate_count\":%d,\"true_match_count\":%d,\"false_positive_count\":%d,\"measurement_status\":\"%s\"}\n",
		r.n, r.ell, float64(r.serverMS)/1000.0, float64(r.serializeUS)/1000.0,
		float64(r.serverMS+r.serializeUS)/1000.0, r.commBytes, r.commBytes,
		r.candidates, r.trueHits, r.falsePositives, r.status)
	fmt.Printf("[RECIPIENT_JSON] {\"scheme\":\"FMD\",\"N\":%d,\"k_actual\":%d,\"candidate_count\":%d,\"response_bytes\":%d,\"recipient_processing_ns\":%.3f,\"recipient_inner_ops\":%d,\"correctness\":%t,\"consumer_operations\":\"deserialize_count_and_all_candidate_uint64_handles\"}\n",
		r.n, r.ell, r.candidates, r.commBytes, r.recipientConsumeNS,
		r.recipientInnerOps, r.recipientConsumeOK)
}

func main() {
	n := flag.Int("N", 4096, "number of tested messages")
	ell := flag.Int("ell", 50, "number of true positives")
	gamma := flag.Int("gamma", 8, "FMD2 false-positive exponent")
	reps := flag.Int("reps", 5, "number of repetitions to average")
	all := flag.Bool("all", false, "run the paper benchmark N set")
	flag.Parse()

	fmt.Println("[BENCH_CSV] scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck")
	if *reps <= 0 {
		fmt.Fprintln(os.Stderr, "-reps must be positive")
		os.Exit(2)
	}
	if *all {
		for _, n := range []int{256, 512, 1024, 2048, 4096, 8192, 16384} {
			printResult(average(n, *ell, *gamma, *reps))
		}
		return
	}
	if *n <= 0 {
		fmt.Fprintln(os.Stderr, "-N must be positive")
		os.Exit(2)
	}
	printResult(average(*n, *ell, *gamma, *reps))
}
