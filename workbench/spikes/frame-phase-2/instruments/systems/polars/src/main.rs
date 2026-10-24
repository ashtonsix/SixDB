use polars::prelude::*;
use std::collections::BTreeMap;

// Default native system allocator. Normal column construction owns the data;
// subsequent plans reuse immutable columns. No imported Arrow/zero-copy law.
fn column(name: &str, dtype: &DataType, values: &[Option<i128>], chunk: usize) -> Series {
    macro_rules! typed {
        ($ty:ty) => {{
            let part = |v: &[Option<i128>]| {
                let owned: Vec<Option<$ty>> = v.iter()
                    .map(|v| v.map(|x| <$ty>::try_from(x).expect("fixture fits logical lane")))
                    .collect();
                Series::new(name.into(), owned.as_slice())
            };
            if values.is_empty() { part(&[]) } else {
                let mut parts = values.chunks(chunk).map(part);
                let mut out = parts.next().unwrap();
                for next in parts { out.append(&next).unwrap(); }
                out
            }
        }};
    }
    match dtype {
        DataType::Int8 => typed!(i8), DataType::Int16 => typed!(i16),
        DataType::Int32 => typed!(i32), DataType::Int64 => typed!(i64),
        DataType::UInt8 => typed!(u8), DataType::UInt16 => typed!(u16),
        DataType::UInt32 => typed!(u32), DataType::UInt64 => typed!(u64),
        _ => unreachable!(),
    }
}

fn threshold(value: i128, dtype: &DataType) -> Expr {
    match dtype {
        DataType::Int8 => lit(i8::try_from(value).unwrap()),
        DataType::Int16 => lit(i16::try_from(value).unwrap()),
        DataType::Int32 => lit(i32::try_from(value).unwrap()),
        DataType::Int64 => lit(i64::try_from(value).unwrap()),
        DataType::UInt8 => lit(u8::try_from(value).unwrap()),
        DataType::UInt16 => lit(u16::try_from(value).unwrap()),
        DataType::UInt32 => lit(u32::try_from(value).unwrap()),
        DataType::UInt64 => lit(u64::try_from(value).unwrap()),
        _ => unreachable!(),
    }
}

// Sum all negative/positive inputs separately. This bounds every subset and
// intermediate, rather than only a cancelling final sum. Small lanes already
// promote to i64 in this Polars epoch; preserve that natural implementation.
fn accumulator(dtype: &DataType, b: &[Option<i128>], signed: bool) -> DataType {
    let (mut lower, mut upper) = (0i128, 0i128);
    for value in b.iter().flatten() {
        if *value < 0 { lower = lower.checked_add(*value).unwrap(); }
        else { upper = upper.checked_add(*value).unwrap(); }
    }
    match dtype {
        DataType::Int8 | DataType::UInt8 | DataType::Int16 | DataType::UInt16 => {
            assert!(lower >= i64::MIN as i128 && upper <= i64::MAX as i128);
            DataType::Int64
        },
        DataType::Int32 if lower >= i32::MIN as i128 && upper <= i32::MAX as i128 => DataType::Int32,
        DataType::UInt32 if upper <= u32::MAX as i128 => DataType::UInt32,
        _ if signed && lower >= i64::MIN as i128 && upper <= i64::MAX as i128 => DataType::Int64,
        _ if signed => DataType::Int128,
        _ if upper <= u64::MAX as i128 => DataType::UInt64,
        _ => DataType::UInt128,
    }
}

fn sum_expr(input: &DataType, output: &DataType) -> Expr {
    let b = col("B");
    if input == output || matches!(input, DataType::Int8 | DataType::Int16 | DataType::UInt8 | DataType::UInt16) {
        b.sum().alias("sum")
    } else { b.cast(output.clone()).sum().alias("sum") }
}

fn scalar(value: AnyValue<'_>) -> i128 {
    match value {
        AnyValue::Int8(x) => x as i128, AnyValue::Int16(x) => x as i128,
        AnyValue::Int32(x) => x as i128, AnyValue::Int64(x) => x as i128,
        AnyValue::Int128(x) => x, AnyValue::UInt8(x) => x as i128,
        AnyValue::UInt16(x) => x as i128, AnyValue::UInt32(x) => x as i128,
        AnyValue::UInt64(x) => x as i128,
        AnyValue::UInt128(x) => i128::try_from(x).expect("fixture sum fits i128"),
        _ => panic!("expected actual integer scalar, got {value:?}"),
    }
}

#[derive(Default)]
struct Coverage {
    queries: usize, zero_rows: usize, selected_all_null_b: usize,
    unequal_counts: usize, multi_chunks: usize, natural: usize, widened: usize,
    checksum: u128, accumulators: BTreeMap<String, usize>,
}

fn check(dtype: DataType, signed: bool, lo: i128, hi: i128, c: &mut Coverage) -> PolarsResult<()> {
    for n in [0, 1, 256, 257, 1025] {
        for nulls in 0..6 { for edge in [false, true] { for layout in 0..3 {
            let a: Vec<_> = (0..n).map(|i| {
                let value = if edge { [lo, hi, 0, 1, hi - 1, lo + 1, 0][i % 7] }
                    else { ((i * 7 + 3) % 101) as i128 - if signed { 50 } else { 0 } };
                if nulls == 4 || ((nulls == 1 || nulls == 3) && i % 5 == 0) { None } else { Some(value) }
            }).collect();
            let b: Vec<_> = (0..n).map(|i| {
                let value = if edge { [hi, hi, lo, hi, 0, lo + 1, hi - 1][i % 7] }
                    else { ((i * 13 + 7) % 101) as i128 - if signed { 50 } else { 0 } };
                if nulls == 5 || ((nulls == 2 || nulls == 3) && i % 7 == 0) { None } else { Some(value) }
            }).collect();
            let (a_chunk, b_chunk) = match layout { 0 => (n.max(1), n.max(1)), 1 => (256, 256), _ => (127, 193) };
            let a_series = column("A", &dtype, &a, a_chunk);
            let b_series = column("B", &dtype, &b, b_chunk);
            let chunks = (a_series.n_chunks(), b_series.n_chunks());
            let frame = DataFrame::new(n, vec![a_series.into(), b_series.into()])?;
            let acc = accumulator(&dtype, &b, signed);
            // Typed literal bindings on unchanged columns. Repetition and a
            // nonmonotonic order expose stale selection/parameter state.
            let thresholds = [lo, hi, 0, 1, hi - 1, lo + 1, if signed { -1 } else { hi }, 0, lo];
            for (binding, value) in thresholds.into_iter().enumerate() {
                let (mut rows, mut count, mut sum_i, mut sum_u) = (0u32, 0u32, 0i128, 0u128);
                for (a, b) in a.iter().zip(&b) {
                    if a.is_some_and(|x| x >= value) {
                        rows += 1;
                        if let Some(b) = b {
                            count += 1;
                            if signed { sum_i = sum_i.checked_add(*b).unwrap(); }
                            else { sum_u = sum_u.checked_add(u128::try_from(*b).unwrap()).unwrap(); }
                        }
                    }
                }
                let query = frame.clone().lazy().filter(col("A").gt_eq(threshold(value, &dtype)))
                    .select([sum_expr(&dtype, &acc), col("B").count().alias("count_b"), len().alias("rows")]);
                if n == 257 && nulls == 3 && edge && layout == 2 && binding == 2 {
                    let plan = query.describe_optimized_plan()?;
                    eprintln!("OPTIMIZED {dtype:?} -> {acc:?}\n{plan}");
                    assert!(plan.contains("FILTER") && plan.contains("sum"));
                }
                let result = query.collect()?;
                assert_eq!(result.shape(), (1, 3));
                let sum = result.column("sum")?;
                assert_eq!(sum.dtype(), &acc, "actual accumulator dtype");
                let actual = scalar(sum.get(0)?);
                let expected = if signed { sum_i } else { i128::try_from(sum_u).unwrap() };
                assert_eq!(actual, expected, "{dtype:?} n={n} nulls={nulls} edge={edge} layout={layout} binding={binding}");
                assert_eq!(scalar(result.column("count_b")?.get(0)?), count as i128);
                assert_eq!(scalar(result.column("rows")?.get(0)?), rows as i128);
                assert_eq!(frame.height(), n, "source remains reusable");
                c.queries += 1; c.zero_rows += usize::from(rows == 0);
                c.selected_all_null_b += usize::from(rows > 0 && count == 0);
                c.unequal_counts += usize::from(rows != count);
                c.multi_chunks += usize::from(chunks.0 > 1 || chunks.1 > 1);
                let natural = dtype == acc || matches!(dtype, DataType::Int8 | DataType::Int16 | DataType::UInt8 | DataType::UInt16);
                c.natural += usize::from(natural); c.widened += usize::from(!natural);
                *c.accumulators.entry(format!("{acc:?}")).or_default() += 1;
                c.checksum = c.checksum.wrapping_add(actual as u128)
                    .wrapping_add((rows as u128) << 64).wrapping_add(count as u128);
            }
        }}}
        println!("{{\"dtype\":\"{dtype:?}\",\"rows\":{n},\"queries\":324,\"pass\":true}}");
    }
    Ok(())
}

fn main() -> PolarsResult<()> {
    let mut c = Coverage::default();
    macro_rules! lane {
        ($dtype:ident, $ty:ty, $signed:expr) => {
            check(DataType::$dtype, $signed, <$ty>::MIN as i128, <$ty>::MAX as i128, &mut c)?;
        };
    }
    lane!(Int8, i8, true); lane!(Int16, i16, true);
    lane!(Int32, i32, true); lane!(Int64, i64, true);
    lane!(UInt8, u8, false); lane!(UInt16, u16, false);
    lane!(UInt32, u32, false); lane!(UInt64, u64, false);
    assert_eq!(c.queries, 12960);
    assert!(c.zero_rows > 0 && c.selected_all_null_b > 0 && c.unequal_counts > 0 && c.multi_chunks > 0);
    println!("{{\"accumulator_queries\":{:?}}}", c.accumulators);
    println!("{{\"peer\":\"native Rust Polars\",\"queries\":{},\"scalar_checks\":{},\"zero_selected_rows\":{},\"selected_all_null_b\":{},\"count_b_differs_from_rows\":{},\"multiple_input_chunks\":{},\"natural_accumulator_queries\":{},\"explicit_widening_queries\":{},\"checksum_mod_u128\":\"{}\",\"polars_sum_empty_and_all_null\":0,\"pass\":true,\"timing_claim\":false}}",
        c.queries, 3 * c.queries, c.zero_rows, c.selected_all_null_b, c.unequal_counts,
        c.multi_chunks, c.natural, c.widened, c.checksum);
    Ok(())
}
