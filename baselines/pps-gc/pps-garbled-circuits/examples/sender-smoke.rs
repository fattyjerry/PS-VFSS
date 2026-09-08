use rand::rngs::OsRng;
use rand::Rng;
use std::env;
use std::fs::{metadata, read_to_string, OpenOptions};
use std::io::{Write};
use std::time::{Instant, SystemTime, UNIX_EPOCH};
use stealth_address_circuits::consts::{INDEX_BYTES, LOCATION_BYTES};

fn append(path: &str, trial: usize, warm: bool, op: &str, elapsed: i128,
          bytes: Option<usize>, bytes_status: &str, status: &str) {
    let fresh = metadata(path).map(|m| m.len() == 0).unwrap_or(true);
    let mut f = OpenOptions::new().create(true).append(true).open(path).unwrap();
    if fresh { writeln!(f, "timestamp,scheme,experiment,operation,role,trial,warmup,message_size_bytes,recipient_input_size,scheme_specific_parameters,wall_time_ns,output_bytes,output_bytes_status,status,git_commit,compiler,compiler_version,build_type,machine_id").unwrap(); }
    let ts = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
    let host = env::var("HOSTNAME").ok().or_else(|| read_to_string("/etc/hostname").ok()).unwrap_or_else(|| "unknown".into()).trim().to_string();
    writeln!(f, "{},PPS-GC,sender_signaling,{},sender,{},{},32,2,recipient_bits=16;location_bytes=32,{},{},{},{},unavailable,rustc,nightly-2021-05-06,Release,{}",
             ts, op, trial, warm, elapsed, bytes.map(|v|v.to_string()).unwrap_or_default(), bytes_status, status, host).unwrap();
    f.sync_data().unwrap();
}

fn generate(rng: &mut OsRng, recipient: u16, value: &[u8; LOCATION_BYTES]) -> (u16,u16,[u8;LOCATION_BYTES],[u8;LOCATION_BYTES]) {
    let ra: u16 = rng.gen(); let rb = ra ^ recipient;
    let la: [u8; LOCATION_BYTES] = rng.gen(); let mut lb = [0u8; LOCATION_BYTES];
    for i in 0..LOCATION_BYTES { lb[i] = la[i] ^ value[i]; }
    (ra, rb, la, lb)
}

fn prepare(ra:u16, rb:u16, la:&[u8;LOCATION_BYTES], lb:&[u8;LOCATION_BYTES]) -> (Vec<u8>,Vec<u8>) {
    let mut a=Vec::with_capacity(INDEX_BYTES+LOCATION_BYTES); let mut b=Vec::with_capacity(INDEX_BYTES+LOCATION_BYTES);
    a.extend_from_slice(&ra.to_be_bytes()); a.extend_from_slice(la);
    b.extend_from_slice(&rb.to_be_bytes()); b.extend_from_slice(lb); (a,b)
}

fn correct(recipient:u16,value:&[u8;LOCATION_BYTES],ra:u16,rb:u16,la:&[u8;LOCATION_BYTES],lb:&[u8;LOCATION_BYTES])->bool {
    (ra^rb)==recipient && (0..LOCATION_BYTES).all(|i|(la[i]^lb[i])==value[i])
}

fn append_scale(path:&str,b:usize,trial:isize,warm:bool,total:i128,per:i128,status:&str){
 let fresh=metadata(path).map(|m|m.len()==0).unwrap_or(true); let mut f=OpenOptions::new().create(true).append(true).open(path).unwrap();
 if fresh { writeln!(f,"timestamp,scheme,experiment,operation,role,B,trial,warmup,total_time_ns,per_signal_time_ns,scheme_specific_parameters,recipient_mode,status").unwrap(); }
 let ts=SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos(); writeln!(f,"{},PPS-GC,multiple_signal_sender_scalability,share_generation,sender,{},{},{},{},{},recipient_bits=16;location_bytes=32,fixed_target_recipient,{}",ts,b,trial,warm,total,per,status).unwrap(); f.sync_data().unwrap();
}

fn main() {
    let raw: Vec<String> = env::args().collect();
    if raw.get(1).map(|x|x=="--mode").unwrap_or(false) {
        if raw.len()!=11 || raw[2]!="scaling" { panic!("usage: --mode scaling --signals B --trials N --warmup N --output CSV"); }
        let mut b=0usize; let mut trials=0usize; let mut warmups=0usize; let mut path="".to_string(); let mut i=3;
        while i<raw.len(){match raw[i].as_str(){"--signals"=>b=raw[i+1].parse().unwrap(),"--trials"=>trials=raw[i+1].parse().unwrap(),"--warmup"=>warmups=raw[i+1].parse().unwrap(),"--output"=>path=raw[i+1].clone(),_=>panic!("unknown argument")};i+=2;}
        let recipient=7u16; let mut value=[0u8;LOCATION_BYTES]; value.copy_from_slice(b"hi, anonymus signal message here"); let mut rng=OsRng;
        for tr in 0..(trials+warmups){let warm=tr<warmups; let start=Instant::now(); let mut last=(0u16,0u16,[0u8;LOCATION_BYTES],[0u8;LOCATION_BYTES]); let mut sink=0u16; for _ in 0..b {last=generate(&mut rng,recipient,&value); sink ^= last.0 ^ last.1; sink ^= last.2[0] as u16 ^ last.3[0] as u16;} unsafe { std::ptr::read_volatile(&sink); } let total=start.elapsed().as_nanos() as i128; let ok=correct(recipient,&value,last.0,last.1,&last.2,&last.3); append_scale(&path,b,tr as isize-warmups as isize,warm,total,total/b as i128,if ok{"ok"}else{"failed"}); if !ok{std::process::exit(1);}}
        return;
    }
    let mut args=env::args(); let _exe=args.next(); let path=args.next().expect("usage: ppsgc_sender_smoke CSV [trials] [warmup]");
    let trials: usize=args.next().and_then(|x|x.parse().ok()).unwrap_or(3); let warmups: usize=args.next().and_then(|x|x.parse().ok()).unwrap_or(1); let recipient=7u16;
    let mut value=[0u8;LOCATION_BYTES]; value.copy_from_slice(b"hi, anonymus signal message here");
    let mut rng=OsRng; let mut previous_ra=None;
    const GEN_INNER: usize = 100; const PREP_INNER: usize = 1000;
    for trial in 0..(trials+warmups) { let warm=trial<warmups; let measured=trial.saturating_sub(warmups);
        let s=Instant::now(); let mut last=(0u16,0u16,[0u8;LOCATION_BYTES],[0u8;LOCATION_BYTES]);
        for _ in 0..GEN_INNER { last=generate(&mut rng,recipient,&value); }
        let gen=s.elapsed().as_nanos() as i128 / GEN_INNER as i128;
        let (ra,rb,la,lb)=last;
        let p=Instant::now(); let mut a=Vec::new(); let mut b=Vec::new();
        for _ in 0..PREP_INNER { let (aa,bb)=prepare(ra,rb,&la,&lb); a=aa; b=bb; }
        let prep=p.elapsed().as_nanos() as i128 / PREP_INNER as i128;
        let ok=correct(recipient,&value,ra,rb,&la,&lb)&&a.len()==INDEX_BYTES+LOCATION_BYTES&&b.len()==INDEX_BYTES+LOCATION_BYTES&&previous_ra!=Some(ra); previous_ra=Some(ra);
        append(&path,measured,warm,"pps_gc_share_generation",gen,Some(a.len()+b.len()),"measured",if ok{"ok"}else{"failed"});
        append(&path,measured,warm,"pps_gc_sender_preparation",prep,Some(a.len()+b.len()),"measured",if ok{"ok"}else{"failed"});
        if !warm { append(&path,measured,false,"message_encryption",-1,None,"not_applicable","not_applicable"); }
        let t=Instant::now(); let mut ta=Vec::new(); let mut tb=Vec::new(); let mut tra=0; let mut trb=0; let mut tla=[0u8;LOCATION_BYTES]; let mut tlb=[0u8;LOCATION_BYTES];
        for _ in 0..GEN_INNER { let z=generate(&mut rng,recipient,&value); tra=z.0; trb=z.1; tla=z.2; tlb=z.3; let (aa,bb)=prepare(tra,trb,&tla,&tlb); ta=aa; tb=bb; }
        let total=t.elapsed().as_nanos() as i128 / GEN_INNER as i128;
        let tok=correct(recipient,&value,tra,trb,&tla,&tlb)&&ta.len()+tb.len()==2*(INDEX_BYTES+LOCATION_BYTES);
        if !warm { append(&path,measured,false,"pps_gc_sender_total_signaling",total,Some(ta.len()+tb.len()),"measured",if tok{"ok"}else{"failed"}); }
        if !ok||!tok { std::process::exit(1); }
    }
    println!("PPS-GC sender smoke ok; per-server=34 total=68 fresh_randomness=true");
}
