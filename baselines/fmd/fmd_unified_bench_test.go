package fuzzycrypto

import (
	"crypto/elliptic"
	"crypto/rand"
	"fmt"
	"testing"
	"time"
)

const (
	unifiedFMDGamma = 24
	unifiedFMDEll   = 50
	unifiedFMDReps  = 5
)

var unifiedFMDNs = []int{256, 512, 1024, 2048, 4096, 8192, 16384}

type unifiedFMDResult struct {
	n           int
	ell         int
	setupMS     int64
	sendMS      int64
	serverMS    int64
	recipientMS int64
	commBytes   int64
	status      string
	bottleneck  string
}

func unifiedElapsedMS(start time.Time) int64 {
	return time.Since(start).Round(time.Millisecond).Milliseconds()
}

func unifiedSlowestFMD(r unifiedFMDResult) string {
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

func runUnifiedFMDOnce(n, ell, gamma int) unifiedFMDResult {
	r := unifiedFMDResult{
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
	var scheme ElGamalPower2

	start := time.Now()
	sk, pk := scheme.KeyGen(curve, gamma, rand.Reader)
	_, decoyPK := scheme.KeyGen(curve, gamma, rand.Reader)
	r.setupMS = unifiedElapsedMS(start)
	if sk == nil || pk == nil || decoyPK == nil {
		r.status = "crashed"
		r.bottleneck = "keygen_failed"
		return r
	}

	start = time.Now()
	dsk := scheme.Extract(gamma, sk)
	r.recipientMS = unifiedElapsedMS(start)
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
	r.sendMS = unifiedElapsedMS(start)

	candidates := make([]int, 0, ell)
	start = time.Now()
	for i, msg := range messages {
		if scheme.Test(curve, msg, dsk) {
			candidates = append(candidates, i)
		}
	}
	r.serverMS = unifiedElapsedMS(start)

	start = time.Now()
	trueHits := 0
	for _, idx := range candidates {
		if idx < ell {
			trueHits++
		}
	}
	r.recipientMS += unifiedElapsedMS(start)
	r.commBytes = int64(len(candidates) * 8)

	if trueHits != ell {
		r.status = "crashed"
		r.bottleneck = fmt.Sprintf("true_positive_mismatch_gamma=%d_p=2^-%d_candidates=%d", gamma, gamma, len(candidates))
		return r
	}
	r.bottleneck = fmt.Sprintf("%s_gamma=%d_p=2^-%d_candidates=%d", unifiedSlowestFMD(r), gamma, gamma, len(candidates))
	return r
}

func averageUnifiedFMD(n, ell, gamma, reps int) unifiedFMDResult {
	avg := unifiedFMDResult{
		n:      n,
		ell:    ell,
		status: "completed",
	}
	var lastBottleneck string
	for i := 0; i < reps; i++ {
		r := runUnifiedFMDOnce(n, ell, gamma)
		avg.setupMS += r.setupMS
		avg.sendMS += r.sendMS
		avg.serverMS += r.serverMS
		avg.recipientMS += r.recipientMS
		avg.commBytes += r.commBytes
		if r.status != "completed" {
			avg.status = r.status
		}
		lastBottleneck = r.bottleneck
	}
	avg.setupMS /= int64(reps)
	avg.sendMS /= int64(reps)
	avg.serverMS /= int64(reps)
	avg.recipientMS /= int64(reps)
	avg.commBytes /= int64(reps)
	avg.bottleneck = fmt.Sprintf("%s_avg_reps=%d", lastBottleneck, reps)
	return avg
}

func printUnifiedFMDCSV(r unifiedFMDResult) {
	fmt.Printf("[BENCH_CSV] FMD,%d,%d,%d,%d,%d,%d,%d,%s,%s\n",
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
}

func BenchmarkUnifiedFMDCSV(b *testing.B) {
	if b.N != 1 {
		return
	}
	b.StopTimer()
	for _, n := range unifiedFMDNs {
		printUnifiedFMDCSV(averageUnifiedFMD(n, unifiedFMDEll, unifiedFMDGamma, unifiedFMDReps))
	}
}
