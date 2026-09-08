use std::env;
use std::time::Instant;

use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use stealth_address_circuits::consts::{INDEX_BYTES, LOCATION_BYTES};
use stealth_address_circuits::{ByteArray, LocationTable, TableSize};

fn main() {
    let args: Vec<String> = env::args().collect();
    let mut ns = 16usize;
    let mut k = 1usize;
    let mut nr = 2usize;
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--Ns" => { i += 1; ns = args[i].parse().unwrap(); }
            "--k" => { i += 1; k = args[i].parse().unwrap(); }
            "--Nr" => { i += 1; nr = args[i].parse().unwrap(); }
            other => panic!("unknown argument: {}", other),
        }
        i += 1;
    }
    if ns == 0 || k == 0 || nr == 0 || nr > u16::MAX as usize || k > ns {
        eprintln!("invalid parameters");
        std::process::exit(2);
    }

    let target = 0u16;
    let size = TableSize { m: nr, l: k };
    let mut rng = StdRng::seed_from_u64(0x505053434c49454e);
    let mut expected = Vec::<[u8; LOCATION_BYTES]>::with_capacity(k);
    for match_idx in 0..k {
        let mut location = [0u8; LOCATION_BYTES];
        location[..8].copy_from_slice(&(match_idx as u64 + 1).to_be_bytes());
        expected.push(location);
    }

    let total_start = Instant::now();
    let signaling_start = Instant::now();
    let mut receiver_a = Vec::with_capacity(ns);
    let mut receiver_b = Vec::with_capacity(ns);
    let mut location_a = Vec::<[u8; LOCATION_BYTES]>::with_capacity(ns);
    let mut location_b = Vec::<[u8; LOCATION_BYTES]>::with_capacity(ns);
    for signal_idx in 0..ns {
        let recipient = if signal_idx < k { target } else { 1u16 % nr as u16 };
        let ra: u16 = rng.gen();
        receiver_a.push(ra);
        receiver_b.push(ra ^ recipient);
        let mut la = [0u8; LOCATION_BYTES];
        rng.fill(&mut la);
        let intended = if signal_idx < k { expected[signal_idx] } else { [0u8; LOCATION_BYTES] };
        let mut lb = [0u8; LOCATION_BYTES];
        for j in 0..LOCATION_BYTES { lb[j] = la[j] ^ intended[j]; }
        location_a.push(la);
        location_b.push(lb);
    }
    let signaling_ns = signaling_start.elapsed().as_nanos();

    let serialization_start = Instant::now();
    let mut outbound_a = Vec::with_capacity(ns * (INDEX_BYTES + LOCATION_BYTES));
    let mut outbound_b = Vec::with_capacity(ns * (INDEX_BYTES + LOCATION_BYTES));
    for signal_idx in 0..ns {
        outbound_a.extend_from_slice(&receiver_a[signal_idx].to_be_bytes());
        outbound_a.extend_from_slice(&location_a[signal_idx]);
        outbound_b.extend_from_slice(&receiver_b[signal_idx].to_be_bytes());
        outbound_b.extend_from_slice(&location_b[signal_idx]);
    }
    let serialization_ns = serialization_start.elapsed().as_nanos();
    let serialization_checksum = outbound_a.iter().chain(outbound_b.iter()).fold(0u64, |acc, byte| acc.wrapping_add(u64::from(*byte)));
    let total_ns = total_start.elapsed().as_nanos();

    // A correctly updated table is the retrieval precondition. This constructs
    // exactly that state; it does not time or substitute for the GC update.
    let mut flat_a = vec![ByteArray::<LOCATION_BYTES>::new([0u8; LOCATION_BYTES]); nr * k];
    let mut flat_b = vec![ByteArray::<LOCATION_BYTES>::new([0u8; LOCATION_BYTES]); nr * k];
    for match_idx in 0..k {
        flat_a[match_idx] = ByteArray::new(location_a[match_idx]);
        flat_b[match_idx] = ByteArray::new(location_b[match_idx]);
    }
    let table_a = LocationTable::new(flat_a.into_boxed_slice(), size).unwrap();
    let table_b = LocationTable::new(flat_b.into_boxed_slice(), size).unwrap();

    let recipient_start = Instant::now();
    let mut recovered = Vec::<[u8; LOCATION_BYTES]>::with_capacity(k);
    for j in 0..k {
        let a = table_a[target][j].as_buffer();
        let b = table_b[target][j].as_buffer();
        let mut location = [0u8; LOCATION_BYTES];
        for x in 0..LOCATION_BYTES { location[x] = a[x] ^ b[x]; }
        recovered.push(location);
    }
    let recipient_ns = recipient_start.elapsed().as_nanos();

    let share_correct = (0..ns).all(|idx| (receiver_a[idx] ^ receiver_b[idx]) == if idx < k { target } else { 1u16 % nr as u16 })
        && (0..ns).all(|idx| (0..LOCATION_BYTES).all(|j| (location_a[idx][j] ^ location_b[idx][j]) == if idx < k { expected[idx][j] } else { 0 }));
    let correctness = share_correct && recovered == expected;
    println!(
        "[CLIENT_BENCH_JSON] {{\"scheme\":\"PPS-GC\",\"Ns\":{},\"Nr\":{},\"actual_k\":{},\"observed_result_count\":{},\"sender_signaling_generation_ns\":{},\"sender_serialization_ns\":{},\"sender_total_online_ns\":{},\"recipient_processing_ns\":{},\"serialized_bytes\":{},\"serialization_checksum\":{},\"correctness\":{},\"output_type\":\"location_row\",\"error_class\":\"{}\",\"scheme_specific_parameters\":\"m={};l={};location_bytes={};index_bytes={}\"}}",
        ns, nr, k, recovered.len(), signaling_ns, serialization_ns, total_ns,
        recipient_ns, outbound_a.len() + outbound_b.len(), serialization_checksum,
        correctness, if correctness { "" } else { "reconstruction_mismatch" },
        nr, k, LOCATION_BYTES, INDEX_BYTES
    );
    if !correctness { std::process::exit(3); }
}
