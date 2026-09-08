use std::env;
use std::convert::TryInto;
use std::iter;
use std::time::Instant;

use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};

use scuttlebutt::unix_channel_pair;

use stealth_address_circuits::circuits::UpdateIndexesStrategy;
use stealth_address_circuits::{
    batched_update_table_evaluator, batched_update_table_garbler, ByteArray, IndexColumn,
    LocationTable, TableSize,
};

const LOCATION_BYTES: usize = 32;

struct BenchResult {
    n: usize,
    ell: usize,
    setup_ms: u128,
    send_ms: u128,
    server_ms: u128,
    admission_us: u128,
    retrieval_core_us: u128,
    serialization_us: u128,
    server0_response_bytes: usize,
    server1_response_bytes: usize,
    recipient_ms: u128,
    comm_bytes: usize,
    status: String,
    bottleneck: String,
}

fn slowest(r: &BenchResult) -> &'static str {
    let mut stage = "setup";
    let mut best = r.setup_ms;
    if r.send_ms > best {
        stage = "send";
        best = r.send_ms;
    }
    if r.server_ms > best {
        stage = "server";
        best = r.server_ms;
    }
    if r.recipient_ms > best {
        stage = "recipient";
    }
    stage
}

fn ceil_div(a: usize, b: usize) -> usize {
    (a + b - 1) / b
}

fn consume_row_share_responses(response_a: &[u8], response_b: &[u8]) -> Result<Vec<[u8; LOCATION_BYTES]>, String> {
    if response_a.len() < 4 || response_b.len() < 4 {
        return Err("truncated row-share response".to_string());
    }
    let count_a = u32::from_le_bytes(response_a[..4].try_into().unwrap()) as usize;
    let count_b = u32::from_le_bytes(response_b[..4].try_into().unwrap()) as usize;
    if count_a != count_b {
        return Err("row-share response counts differ".to_string());
    }
    let expected_len = 4 + count_a * LOCATION_BYTES;
    if response_a.len() != expected_len || response_b.len() != expected_len {
        return Err("invalid row-share response length".to_string());
    }
    let mut recovered = Vec::with_capacity(count_a);
    for j in 0..count_a {
        let offset = 4 + j * LOCATION_BYTES;
        let mut location = [0u8; LOCATION_BYTES];
        for i in 0..LOCATION_BYTES {
            location[i] = response_a[offset + i] ^ response_b[offset + i];
        }
        recovered.push(location);
    }
    Ok(recovered)
}

fn run(n: usize, ell: usize) -> BenchResult {
    let mut result = BenchResult {
        n,
        ell,
        setup_ms: 0,
        send_ms: 0,
        server_ms: 0,
        admission_us: 0,
        retrieval_core_us: 0,
        serialization_us: 0,
        server0_response_bytes: 0,
        server1_response_bytes: 0,
        recipient_ms: 0,
        comm_bytes: 0,
        status: "completed".to_string(),
        bottleneck: String::new(),
    };

    if n == 0 || ell == 0 {
        result.status = "crashed".to_string();
        result.bottleneck = "invalid_parameters".to_string();
        return result;
    }

    let table_size = TableSize { m: ceil_div(n, ell), l: ell };
    eprintln!("[BENCH_STAGE] PPS-GC mapped_params m={} l={} n={}", table_size.m, table_size.l, n);
    if table_size.m > u16::MAX as usize {
        result.status = "crashed".to_string();
        result.bottleneck = "receiver_index_exceeds_u16".to_string();
        return result;
    }

    let setup_start = Instant::now();
    let mut table_a = LocationTable::random(&mut StdRng::seed_from_u64(1), table_size).unwrap();
    let mut table_b = LocationTable::random(&mut StdRng::seed_from_u64(1), table_size).unwrap();
    let mut last_upd_table_a =
        IndexColumn::random(&mut StdRng::seed_from_u64(2), table_size.m).unwrap();
    let mut last_upd_table_b =
        IndexColumn::random(&mut StdRng::seed_from_u64(2), table_size.m).unwrap();
    result.setup_ms = setup_start.elapsed().as_millis();

    let mut rng = StdRng::seed_from_u64(0xea15511);
    let send_start = Instant::now();
    let receivers = (0..table_size.m).cycle().take(n);
    let receivers_a: Vec<u16> = iter::repeat_with(|| rng.gen()).take(n).collect();
    let receivers_b: Vec<u16> = receivers
        .zip(&receivers_a)
        .map(|(receiver, blinding)| receiver as u16 ^ blinding)
        .collect();

    let locs: Vec<Vec<u8>> = (0..n).map(|i| {
        let mut location = vec![0u8; LOCATION_BYTES];
        location[..8].copy_from_slice(&((i + 1) as u64).to_le_bytes());
        location
    }).collect();
    let mut gen_loc = || iter::repeat_with(|| rng.gen()).take(LOCATION_BYTES).collect::<Vec<u8>>();
    let locs_a: Vec<Vec<u8>> = iter::repeat_with(gen_loc).take(n).collect();
    let locs_b: Vec<Vec<u8>> = locs
        .iter()
        .zip(&locs_a)
        .map(|(loc, blinding)| loc.iter().zip(blinding).map(|(a, b)| a ^ b).collect())
        .collect();
    result.send_ms = send_start.elapsed().as_millis();

    eprintln!("[BENCH_STAGE] PPS-GC server_batched_update_start");
    let server_start = Instant::now();
    let batch_size = env::var("PPS_BATCH_SIZE")
        .ok()
        .and_then(|v| v.parse::<usize>().ok())
        .filter(|&v| v > 0)
        .unwrap_or(ell);
    eprintln!("[BENCH_STAGE] PPS-GC native_batch_size={}", batch_size);

    for (batch_idx, start) in (0..n).step_by(batch_size).enumerate() {
        let end = usize::min(start + batch_size, n);
        eprintln!(
            "[BENCH_STAGE] PPS-GC batch_start idx={} start={} end={}",
            batch_idx, start, end
        );
        let receivers_a_batch = receivers_a[start..end].to_vec();
        let receivers_b_batch = receivers_b[start..end].to_vec();
        let locs_a_batch = locs_a[start..end].to_vec();
        let locs_b_batch = locs_b[start..end].to_vec();

        let seed_a = StdRng::seed_from_u64(0xdead + batch_idx as u64);
        let seed_b = StdRng::seed_from_u64(0xbeaf + batch_idx as u64);

        let (channel_a, channel_b) = unix_channel_pair();
        let (mut s, last_upd_table, receivers) = (
            seed_a.clone(),
            last_upd_table_a.clone(),
            receivers_a_batch.clone(),
        );
        let table_b_input = table_b.clone();
        let last_upd_table_b_input = last_upd_table_b.clone();
        let handle = std::thread::spawn(move || {
            batched_update_table_garbler(
                channel_a,
                &mut s,
                table_size,
                &last_upd_table,
                UpdateIndexesStrategy::A,
                receivers,
            )
        });
        let (new_table_b, new_indexes_b) = batched_update_table_evaluator(
            channel_b,
            &mut seed_b.clone(),
            &table_b_input,
            &last_upd_table_b_input,
            UpdateIndexesStrategy::A,
            receivers_b_batch
                .iter()
                .zip(&locs_b_batch)
                .map(|(&r, l)| (r, l.as_slice())),
        )
        .unwrap();
        handle.join().unwrap().unwrap();

        let (channel_a, channel_b) = unix_channel_pair();
        let last_upd_table = last_upd_table_b.clone();
        let receivers = receivers_b_batch.clone();
        let table_a_input = table_a.clone();
        let last_upd_table_a_input = last_upd_table_a.clone();
        let handle = std::thread::spawn(move || {
            batched_update_table_garbler(
                channel_b,
                &mut seed_b.clone(),
                table_size,
                &last_upd_table,
                UpdateIndexesStrategy::B,
                receivers,
            )
        });
        let (new_table_a, new_indexes_a) = batched_update_table_evaluator(
            channel_a,
            &mut seed_a.clone(),
            &table_a_input,
            &last_upd_table_a_input,
            UpdateIndexesStrategy::B,
            receivers_a_batch
                .iter()
                .zip(&locs_a_batch)
                .map(|(&r, l)| (r, l.as_slice())),
        )
        .unwrap();
        handle.join().unwrap().unwrap();

        table_a = new_table_a;
        table_b = new_table_b;
        last_upd_table_a = new_indexes_a;
        last_upd_table_b = new_indexes_b;
        eprintln!("[BENCH_STAGE] PPS-GC batch_done idx={}", batch_idx);
    }
    result.admission_us = server_start.elapsed().as_micros();
    result.server_ms = result.admission_us / 1000;

    eprintln!("[BENCH_STAGE] PPS-GC recipient_reconstruction_start");
    let retrieval_start = Instant::now();
    let receiver = 0u16;
    let expected = (0..n)
        .filter(|i| i % table_size.m == receiver as usize)
        .count();
    let recovered_count = (last_upd_table_a[receiver] ^ last_upd_table_b[receiver]).as_buffer().clone();
    let recovered_count = u16::from_be_bytes([recovered_count[0], recovered_count[1]]) as usize;
    let row_a: Vec<ByteArray<32>> = (0..ell).map(|j| table_a[receiver][j]).collect();
    let row_b: Vec<ByteArray<32>> = (0..ell).map(|j| table_b[receiver][j]).collect();
    result.retrieval_core_us = retrieval_start.elapsed().as_micros();

    let serialize_start = Instant::now();
    let serialize_row = |row: &[ByteArray<32>]| {
        let mut buffer = Vec::with_capacity(4 + row.len() * LOCATION_BYTES);
        buffer.extend_from_slice(&(row.len() as u32).to_le_bytes());
        for entry in row {
            buffer.extend_from_slice(entry.as_buffer());
        }
        buffer
    };
    let response_a = serialize_row(&row_a);
    let response_b = serialize_row(&row_b);
    result.serialization_us = serialize_start.elapsed().as_micros();
    result.server0_response_bytes = response_a.len();
    result.server1_response_bytes = response_b.len();
    result.comm_bytes = response_a.len() + response_b.len();

    let recipient_start = Instant::now();
    for j in 0..usize::min(expected, ell) {
        let _row_entry: ByteArray<32> = row_a[j] ^ row_b[j];
    }
    result.recipient_ms = recipient_start.elapsed().as_millis();

    if recovered_count != expected {
        result.status = "crashed".to_string();
        result.bottleneck = format!(
            "row_reconstruction_mismatch_m={}_l={}_receiver={}_expected={}_recovered={}",
            table_size.m, table_size.l, receiver, expected, recovered_count
        );
    } else {
        result.bottleneck = format!(
            "{}_m={}_l={}_receiver_count={}_batch={}",
            slowest(&result),
            table_size.m,
            table_size.l,
            table_size.m,
            batch_size
        );
    }

    result
}

fn print_result(r: &BenchResult) {
    println!(
        "[BENCH_CSV] PPS-GC,{},{},{},{},{},{},{},{},{}",
        r.n,
        r.ell,
        r.setup_ms,
        r.send_ms,
        r.server_ms,
        r.recipient_ms,
        r.comm_bytes,
        r.status,
        r.bottleneck
    );
    println!(
        "[PILOT_JSON] {{\"scheme\":\"PPS-GC\",\"N\":{},\"k_actual\":{},\"admission_status\":\"{}\",\"admission_online_ms\":{:.6},\"retrieval_core_ms\":{:.6},\"response_serialization_ms\":{:.6},\"retrieval_online_ms\":{:.6},\"server0_response_bytes\":{},\"server1_response_bytes\":{},\"server_to_recipient_bytes\":{},\"candidate_count\":null,\"false_positive_count\":null,\"measurement_status\":\"{}\"}}",
        r.n, r.ell, if r.status == "completed" { "ok" } else { "error" },
        r.admission_us as f64 / 1000.0,
        r.retrieval_core_us as f64 / 1000.0,
        r.serialization_us as f64 / 1000.0,
        (r.retrieval_core_us + r.serialization_us) as f64 / 1000.0,
        r.server0_response_bytes, r.server1_response_bytes, r.comm_bytes, r.status
    );
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut n = 4096usize;
    let mut ell = 50usize;
    let mut all = false;
    let mut response_only = false;
    let mut recipient_only = false;
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--N" => {
                i += 1;
                n = args[i].parse().unwrap();
            }
            "--ell" => {
                i += 1;
                ell = args[i].parse().unwrap();
            }
            "--all" => all = true,
            "--response-only-component-pilot" => response_only = true,
            "--recipient-only-component-pilot" => recipient_only = true,
            _ => panic!("unknown argument: {}", args[i]),
        }
        i += 1;
    }

    println!("[BENCH_CSV] scheme,N,ell,setup_ms,send_ms,server_ms,recipient_ms,comm_bytes,status,bottleneck");
    if recipient_only {
        let mut rng = StdRng::seed_from_u64(0x5050534743524543);
        let mut row_a = Vec::with_capacity(ell);
        let mut row_b = Vec::with_capacity(ell);
        let mut expected = Vec::with_capacity(ell);
        for j in 0..ell {
            let share_a: [u8; LOCATION_BYTES] = rng.gen();
            let mut location = [0u8; LOCATION_BYTES];
            location[..8].copy_from_slice(&((j + 1) as u64).to_le_bytes());
            let mut share_b = [0u8; LOCATION_BYTES];
            for i in 0..LOCATION_BYTES { share_b[i] = share_a[i] ^ location[i]; }
            row_a.push(ByteArray::new(share_a));
            row_b.push(ByteArray::new(share_b));
            expected.push(location);
        }
        let serialize_row = |row: &[ByteArray<32>]| {
            let mut buffer = Vec::with_capacity(4 + row.len() * LOCATION_BYTES);
            buffer.extend_from_slice(&(row.len() as u32).to_le_bytes());
            for entry in row { buffer.extend_from_slice(entry.as_buffer()); }
            buffer
        };
        let response_a = serialize_row(&row_a);
        let response_b = serialize_row(&row_b);
        const INNER_OPS: usize = 10_000;
        let start = Instant::now();
        let mut recovered = Vec::new();
        let mut recipient_checksum = 0u8;
        for _ in 0..INNER_OPS {
            recovered = consume_row_share_responses(&response_a, &response_b).unwrap();
            recipient_checksum ^= recovered.last().map(|v| v[0]).unwrap_or(0);
        }
        let recipient_processing_ns = start.elapsed().as_nanos() as f64 / INNER_OPS as f64;
        println!(
            "[RECIPIENT_JSON] {{\"scheme\":\"PPS-GC\",\"N\":{},\"k_actual\":{},\"response_bytes\":{},\"recipient_processing_ns\":{:.3},\"recipient_inner_ops\":{},\"correctness\":{},\"consumer_operations\":\"deserialize_two_row_share_buffers_and_xor_reconstruct\"}}",
            n, ell, response_a.len() + response_b.len(), recipient_processing_ns,
            INNER_OPS, recovered == expected && recipient_checksum == 0
        );
        return;
    }
    if response_only {
        let table_size = TableSize { m: ceil_div(n, ell), l: ell };
        let table_a = LocationTable::random(&mut StdRng::seed_from_u64(1), table_size).unwrap();
        let table_b = LocationTable::random(&mut StdRng::seed_from_u64(2), table_size).unwrap();
        let row_a: Vec<ByteArray<32>> = (0..ell).map(|j| table_a[0][j]).collect();
        let row_b: Vec<ByteArray<32>> = (0..ell).map(|j| table_b[0][j]).collect();
        let serialize_row = |row: &[ByteArray<32>]| {
            let mut buffer = Vec::with_capacity(4 + row.len() * LOCATION_BYTES);
            buffer.extend_from_slice(&(row.len() as u32).to_le_bytes());
            for entry in row { buffer.extend_from_slice(entry.as_buffer()); }
            buffer
        };
        let start = Instant::now();
        let response_a = serialize_row(&row_a);
        let response_b = serialize_row(&row_b);
        let serialization_us = start.elapsed().as_micros();
        println!(
            "[PILOT_JSON] {{\"scheme\":\"PPS-GC\",\"N\":{},\"k_actual\":{},\"admission_status\":\"not_measured\",\"admission_online_ms\":null,\"retrieval_core_ms\":0.0,\"response_serialization_ms\":{:.6},\"retrieval_online_ms\":{:.6},\"server0_response_bytes\":{},\"server1_response_bytes\":{},\"server_to_recipient_bytes\":{},\"measurement_quality\":\"response_only_component_pilot\",\"measurement_status\":\"ok\"}}",
            n, ell, serialization_us as f64 / 1000.0, serialization_us as f64 / 1000.0,
            response_a.len(), response_b.len(), response_a.len() + response_b.len()
        );
        return;
    }
    if all {
        for n in [256usize, 512, 1024, 2048, 4096, 8192, 16384] {
            print_result(&run(n, ell));
        }
    } else {
        print_result(&run(n, ell));
    }
}
