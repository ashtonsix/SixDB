use polars::prelude::*;
use std::collections::BTreeMap;
use std::time::Instant;

#[derive(Clone, Copy, PartialEq)]
enum Op { Sum, Count, Pair }
impl Op { fn name(self) -> &'static str { match self { Self::Sum => "sum", Self::Count => "count", Self::Pair => "pair" } } }
#[derive(Debug, PartialEq)]
struct Answer { sum: u128, count: usize }
fn expected(a: &[u64], b: &[u64], op: Op, threshold: u64) -> Answer {
    let mut out = Answer { sum: 0, count: 0 };
    for (a, b) in a.iter().zip(b) { if op == Op::Sum || *a >= threshold {
        out.count += 1;
        if op != Op::Count { out.sum += *b as u128; }
    }}
    out
}
fn integer(value: AnyValue<'_>) -> u128 {
    match value {
        AnyValue::UInt32(x) => x as u128, AnyValue::UInt64(x) => x as u128,
        AnyValue::UInt128(x) => x,
        AnyValue::Int64(x) => u128::try_from(x).unwrap(),
        AnyValue::Int128(x) => u128::try_from(x).unwrap(),
        _ => panic!("not an exact nonnegative integer: {value:?}"),
    }
}
struct Query {
    plan: LazyFrame, op: Op, rows: usize,
    construction_ns: u128, preparation_ns: u128,
    visible_payload_bytes: usize, sum_output: DataType, sum_route: &'static str,
    a_base: u64, b_base: u64, a_lane: usize, b_lane: usize,
}
fn column(name: &str, values: &[u64], typed_for: bool) -> (Series, u64, u64, DataType, usize) {
    let base = if typed_for { values.iter().copied().min().unwrap_or(0) } else { 0 };
    let maximum = values.iter().copied().max().unwrap_or(0);
    let span = maximum - base;
    macro_rules! narrow {
        ($ty:ty, $dtype:ident, $bytes:expr) => {{
            let residuals: Vec<$ty> = values.iter().map(|v| <$ty>::try_from(*v - base).unwrap()).collect();
            let series = Series::new(name.into(), residuals.as_slice());
            assert_eq!(series.dtype(), &DataType::$dtype);
            (series, base, maximum, DataType::$dtype, $bytes)
        }};
    }
    if typed_for && span <= u8::MAX as u64 { narrow!(u8, UInt8, 1) }
    else if typed_for && span <= u16::MAX as u64 { narrow!(u16, UInt16, 2) }
    else if typed_for && span <= u32::MAX as u64 { narrow!(u32, UInt32, 4) }
    else { narrow!(u64, UInt64, 8) }
}
impl Query {
    fn new(a: &[u64], b: &[u64], op: Op, threshold: u64, typed_for: bool) -> PolarsResult<Self> {
        assert_eq!(a.len(), b.len());
        let start = Instant::now();
        let (mut a_base, mut b_base, mut a_max, mut a_lane, mut b_lane) = (0, 0, u64::MAX, 0, 0);
        let (mut a_dtype, mut b_dtype) = (DataType::UInt64, DataType::UInt64);
        let mut columns = Vec::new();
        if op != Op::Sum {
            let (s, base, maximum, dtype, bytes) = column("A", a, typed_for);
            columns.push(s.into()); a_base = base; a_max = maximum; a_dtype = dtype; a_lane = bytes;
        }
        if op != Op::Count {
            let (s, base, _, dtype, bytes) = column("B", b, typed_for);
            columns.push(s.into()); b_base = base; b_dtype = dtype; b_lane = bytes;
        }
        let frame = DataFrame::new(a.len(), columns)?;
        let visible_payload_bytes = frame.estimated_size();
        let construction_ns = start.elapsed().as_nanos();
        let prepare_start = Instant::now();
        // Nonnegative inputs: the full total bounds every subset/intermediate.
        let upper = if op == Op::Count { 0 } else { b.iter().map(|x| (*x - b_base) as u128).sum::<u128>() };
        let (sum_output, sum_route) = if op == Op::Count { (DataType::UInt64, "not_requested") }
            else if matches!(b_dtype, DataType::UInt8 | DataType::UInt16) { (DataType::Int64, "Int64_small_lane_natural") }
            else if b_dtype == DataType::UInt32 && upper <= u32::MAX as u128 { (DataType::UInt32, "UInt32_bounded_natural") }
            else if upper <= u64::MAX as u128 { (DataType::UInt64, if b_dtype == DataType::UInt64 { "UInt64_bounded_natural" } else { "UInt64_query_cast" }) }
            else { (DataType::Int128, "Int128_query_cast") };
        let body = if b_dtype == sum_output || matches!(b_dtype, DataType::UInt8 | DataType::UInt16) { col("B") } else { col("B").cast(sum_output.clone()) };
        let sum = body.sum().alias("sum");
        let predicate = if threshold <= a_base { lit(true) } else if threshold > a_max { lit(false) } else {
            let value = threshold - a_base;
            let rhs = match a_dtype {
                DataType::UInt8 => lit(u8::try_from(value).unwrap()),
                DataType::UInt16 => lit(u16::try_from(value).unwrap()),
                DataType::UInt32 => lit(u32::try_from(value).unwrap()),
                _ => lit(value),
            };
            col("A").gt_eq(rhs)
        };
        let plan = match op {
            Op::Sum => frame.lazy().select([sum]),
            Op::Count => frame.lazy().filter(predicate).select([len().alias("count")]),
            Op::Pair => frame.lazy().filter(predicate).select([sum, len().alias("count")]),
        };
        Ok(Self { plan, op, rows: a.len(), construction_ns,
            preparation_ns: prepare_start.elapsed().as_nanos(), visible_payload_bytes, sum_output, sum_route,
            a_base, b_base, a_lane, b_lane })
    }
    fn execute(&self) -> PolarsResult<Answer> {
        // Logical query construction is retained. Clone, optimization, engine
        // execution, scalar consumption and result destruction stay in this call.
        let frame = self.plan.clone().collect()?;
        assert_eq!(frame.height(), 1);
        let sum = if self.op == Op::Count { 0 } else {
            let col = frame.column("sum")?;
            assert_eq!(col.dtype(), &self.sum_output);
            integer(col.get(0)?)
        };
        let count = if self.op == Op::Sum { self.rows } else { usize::try_from(integer(frame.column("count")?.get(0)?)).unwrap() };
        Ok(Answer { sum: sum + self.b_base as u128 * count as u128, count })
    }
}
fn load(directory: &str, name: &str, rows: usize) -> Vec<u64> {
    use std::io::Read;
    let mut file = std::fs::File::open(format!("{directory}/{name}.u64")).unwrap();
    let mut bytes = vec![0u8; rows * 8]; file.read_exact(&mut bytes).unwrap();
    bytes.chunks_exact(8).map(|b| u64::from_le_bytes(b.try_into().unwrap())).collect()
}
fn quantile(values: &[u64], percent: usize) -> u64 {
    let mut sorted = values.to_vec(); sorted.sort_unstable(); sorted[sorted.len() * percent / 100]
}
fn qualify() -> PolarsResult<()> {
    let fixtures = [vec![], vec![0, 0, 0], vec![0, 1, u64::MAX, u64::MAX - 1], vec![u64::MAX - 255, u64::MAX - 3, u64::MAX]];
    for typed_for in [false, true] { for a in &fixtures { for op in [Op::Sum, Op::Count, Op::Pair] { for threshold in [0, 1, u64::MAX] {
        let query = Query::new(a, a, op, threshold, typed_for)?;
        assert_eq!(query.execute()?, expected(&a, &a, op, threshold));
    }}}}
    let a_base = (1u64 << 63) + 65536;
    let b_base = 1u64 << 62;
    let distinct = [
        (vec![a_base, a_base + 300, a_base + 65535, a_base + 1000, a_base + 40000],
         vec![b_base + u32::MAX as u64, b_base + (1u64 << 31) + 17, b_base, b_base + 37, b_base + u32::MAX as u64 - 13],
         a_base + 1000, 2usize, 4usize),
        (vec![a_base + u32::MAX as u64, a_base, a_base + 70000, a_base + 2100000000, a_base + 17],
         vec![b_base, b_base + 65535, b_base + 700, b_base + 1, b_base + 40000],
         a_base + 70000, 4usize, 2usize),
    ];
    for typed_for in [false, true] { for (a, b, interior, a_lane, b_lane) in &distinct {
        for op in [Op::Sum, Op::Count, Op::Pair] { for threshold in [0, *interior, u64::MAX] {
            let query = Query::new(a, b, op, threshold, typed_for)?;
            if typed_for {
                if op != Op::Sum { assert_eq!(query.a_lane, *a_lane); }
                if op != Op::Count {
                    assert_eq!(query.b_lane, *b_lane);
                    if *b_lane == 4 {
                        // Even the interior selected residual total exceeds UInt32.
                        let chosen = expected(a, b, Op::Pair, *interior);
                        assert!(chosen.sum - b_base as u128 * chosen.count as u128 > u32::MAX as u128);
                        assert_eq!(query.sum_output, DataType::UInt64);
                        assert_eq!(query.sum_route, "UInt64_query_cast");
                    } else {
                        assert_eq!(query.sum_output, DataType::Int64);
                        assert_eq!(query.sum_route, "Int64_small_lane_natural");
                    }
                }
            }
            assert_eq!(query.execute()?, expected(a, b, op, threshold));
        }}
    }}
    eprintln!("unsigned/extrema/empty/all-none/distinct-column qualification: 108 cases passed");
    Ok(())
}
fn measure(label: &str, an: &str, bn: &str, columns: &BTreeMap<&str, Vec<u64>>, op: Op, percent: usize, rounds: usize, typed_for: bool) -> PolarsResult<()> {
    let a = &columns[an]; let b = &columns[bn];
    let threshold = if op == Op::Sum { 0 } else { quantile(a, percent) };
    let want = expected(a, b, op, threshold);
    let touched = a.chunks(256).filter(|chunk| op == Op::Sum || chunk.iter().any(|v| *v >= threshold)).count();
    let query = Query::new(a, b, op, threshold, typed_for)?;
    assert_eq!(query.execute()?, want);
    let mut samples = Vec::new();
    for _ in 0..rounds {
        let start = Instant::now(); let actual = query.execute()?;
        samples.push(start.elapsed().as_nanos()); assert_eq!(actual, want);
    }
    let mut sorted = samples.clone(); sorted.sort_unstable();
    let source_bytes = a.capacity() * 8 + if op == Op::Pair && an != bn { b.capacity() * 8 } else { 0 };
    let arm = if typed_for { "external_typed_for" } else { "sidecar_uint64" };
    println!("{{\"engine\":\"polars\",\"version\":\"0.54.4\",\"case\":\"{label}\",\"arm\":\"{arm}\",\"op\":\"{}\",\"a\":\"{an}\",\"b\":\"{bn}\",\"a_base\":\"{}\",\"b_base\":\"{}\",\"a_lane_bytes\":{},\"b_lane_bytes\":{},\"rows\":{},\"threshold\":\"{threshold}\",\"selected_rows\":{},\"touched_blocks\":{touched},\"sum\":\"{}\",\"sum_dtype\":\"{}\",\"construction_ns\":{},\"preparation_ns\":{},\"known_source_capacity_bytes\":{source_bytes},\"engine_visible_payload_bytes\":{},\"engine_retained_capacity_bytes\":null,\"condition\":\"repeated_after_untimed_warm_query\",\"query_boundary\":\"clone_logical_plan_collect_consume_destroy\",\"threads\":1,\"median_ns\":{},\"samples_ns\":{:?}}}",
        op.name(), query.a_base, query.b_base, query.a_lane, query.b_lane, a.len(), want.count, want.sum, query.sum_route, query.construction_ns, query.preparation_ns, query.visible_payload_bytes, sorted[sorted.len() / 2], samples);
    Ok(())
}
fn main() -> PolarsResult<()> {
    let args: Vec<_> = std::env::args().collect();
    if args.len() == 2 && args[1] == "--check-only" {
        qualify()?; println!("{{\"checks\":108,\"pass\":true}}"); return Ok(());
    }
    assert_eq!(args.len(), 4, "usage: frame3-polars-corpus COLS_DIR ROWS ROUNDS");
    let rows = args[2].parse::<usize>().unwrap(); let rounds = args[3].parse::<usize>().unwrap();
    assert!(rows > 0 && rows <= 65536 && rows % 256 == 0 && rounds > 0 && rounds <= 101);
    qualify()?;
    let mut columns = BTreeMap::new();
    for name in ["OS", "URLHash", "EventTime", "WatchID", "ClientIP"] { columns.insert(name, load(&args[1], name, rows)); }
    for typed_for in [false, true] {
    for name in ["OS", "URLHash"] {
        measure(&format!("sum_{name}"), name, name, &columns, Op::Sum, 0, rounds, typed_for)?;
        measure(&format!("count_{name}"), name, name, &columns, Op::Count, 50, rounds, typed_for)?;
    }
    measure("pair_EventTime_ClientIP", "EventTime", "ClientIP", &columns, Op::Pair, 90, rounds, typed_for)?;
    measure("pair_WatchID_ClientIP", "WatchID", "ClientIP", &columns, Op::Pair, 90, rounds, typed_for)?;
    }
    Ok(())
}
