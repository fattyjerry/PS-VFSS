package main

import (
	"crypto/elliptic"
	"crypto/rand"
	"encoding/csv"
	"fmt"
	"os"
	"runtime"
	"strconv"
	"time"

	fuzzy "github.com/becgabri/fuzzycrypto"
)

var header = []string{"timestamp", "scheme", "experiment", "operation", "role", "trial", "warmup", "message_size_bytes", "recipient_input_size", "scheme_specific_parameters", "wall_time_ns", "output_bytes", "output_bytes_status", "status", "git_commit", "compiler", "compiler_version", "build_type", "machine_id"}

func appendRow(path string, trial int, warm bool, op string, elapsed int64, outputBytes, byteStatus, status string) error {
	_, err := os.Stat(path); fresh := os.IsNotExist(err)
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644); if err != nil { return err }; defer f.Close()
	w := csv.NewWriter(f); if fresh { _ = w.Write(header) }
	host, _ := os.Hostname()
	record := []string{time.Now().UTC().Format(time.RFC3339Nano), "FMD", "sender_signaling", op, "sender", strconv.Itoa(trial), strconv.FormatBool(warm), "not_applicable", "unclear", "gamma=8;p=2^-8;curve=P-256", strconv.FormatInt(elapsed,10), outputBytes, byteStatus, status, "unavailable", "gc", runtime.Version(), "Release", host}
	if err := w.Write(record); err != nil { return err }; w.Flush(); if err := w.Error(); err != nil { return err }; return f.Sync()
}

func appendScale(path string, B, trial int, warm bool, total, per int64, status string) error {
	fresh := false; if _, err := os.Stat(path); os.IsNotExist(err) { fresh = true }
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644); if err != nil { return err }; defer f.Close()
	w := csv.NewWriter(f); if fresh { _ = w.Write([]string{"timestamp","scheme","experiment","operation","role","B","trial","warmup","total_time_ns","per_signal_time_ns","scheme_specific_parameters","recipient_mode","status"}) }
	w.Write([]string{time.Now().UTC().Format(time.RFC3339Nano),"FMD","multiple_signal_sender_scalability","flag_generation","sender",strconv.Itoa(B),strconv.Itoa(trial),strconv.FormatBool(warm),strconv.FormatInt(total,10),strconv.FormatInt(per,10),"gamma=8;p=2^-8;curve=P-256","fixed_target_recipient",status}); w.Flush(); return f.Sync()
}

func main() {
	if len(os.Args) >= 2 && os.Args[1] == "--mode" {
		if len(os.Args) != 11 || os.Args[2] != "scaling" { panic("usage: --mode scaling --signals B --trials N --warmup N --output CSV") }
		B,trials,warmups := 0,0,0; out := ""
		for i:=3;i<len(os.Args);i+=2 { switch os.Args[i] { case "--signals": B,_=strconv.Atoi(os.Args[i+1]); case "--trials": trials,_=strconv.Atoi(os.Args[i+1]); case "--warmup": warmups,_=strconv.Atoi(os.Args[i+1]); case "--output": out=os.Args[i+1]; default: panic("unknown argument") } }
		curve:=elliptic.P256(); const gamma=8; var scheme fuzzy.ElGamalPower2; sk,pk:=scheme.KeyGen(curve,gamma,rand.Reader); dsk:=scheme.Extract(gamma,sk); if pk==nil||dsk==nil { panic("FMD setup failed") }
		for tr:=0; tr<warmups+trials; tr++ { warm:=tr<warmups; start:=time.Now(); var last []byte; for i:=0;i<B;i++ { last=scheme.Flag(curve,rand.Reader,pk); if len(last)==0 { return } }; total:=time.Since(start).Nanoseconds(); ok:=len(last)>0&&scheme.Test(curve,last,dsk); if err:=appendScale(out,B,tr-warmups,warm,total,total/int64(B),map[bool]string{true:"ok",false:"failed"}[ok]);err!=nil{panic(err)}; if !ok{return} }
		return
	}
	if len(os.Args) < 2 || len(os.Args) > 4 { fmt.Fprintln(os.Stderr, "usage: fmd_sender_smoke CSV [trials] [warmup]"); os.Exit(2) }
	trials, warmups := 3, 1
	if len(os.Args) >= 3 { trials, _ = strconv.Atoi(os.Args[2]) }
	if len(os.Args) >= 4 { warmups, _ = strconv.Atoi(os.Args[3]) }
	curve := elliptic.P256(); const gamma = 8
	var scheme fuzzy.ElGamalPower2
	sk, pk := scheme.KeyGen(curve, gamma, rand.Reader)
	dsk := scheme.Extract(gamma, sk)
	if pk == nil || dsk == nil { panic("FMD setup failed") }
	for trial := 0; trial < trials+warmups; trial++ {
		warm := trial < warmups; measured := trial-warmups
		start := time.Now(); flag := scheme.Flag(curve, rand.Reader, pk); elapsed := time.Since(start).Nanoseconds()
		ok := len(flag) > 0 && scheme.Test(curve, flag, dsk)
		if err := appendRow(os.Args[1], measured, warm, "fmd_flag_generation", elapsed, strconv.Itoa(len(flag)), "measured", map[bool]string{true:"ok",false:"failed"}[ok]); err != nil { panic(err) }
		if !warm { _ = appendRow(os.Args[1], measured, false, "fmd_sender_preparation", -1, "", "not_applicable", "not_applicable"); _ = appendRow(os.Args[1], measured, false, "message_encryption", -1, "", "not_applicable", "not_applicable") }
		start = time.Now(); totalFlag := scheme.Flag(curve, rand.Reader, pk); totalElapsed := time.Since(start).Nanoseconds()
		totalOK := len(totalFlag) > 0 && scheme.Test(curve, totalFlag, dsk)
		if err := appendRow(os.Args[1], measured, warm, "fmd_sender_total_signaling", totalElapsed, strconv.Itoa(len(totalFlag)), "measured", map[bool]string{true:"ok",false:"failed"}[totalOK]); err != nil { panic(err) }
		if !ok || !totalOK { os.Exit(1) }
	}
	fmt.Println("FMD sender smoke ok; gamma=8 implies p=2^-8")
}
