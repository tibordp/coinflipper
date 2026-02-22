use std::io::Write;

use num_format::{Locale, ToFormattedString};

use crate::pb;
use crate::pb::coin_flipper_client::CoinFlipperClient;
use crate::stats::result_array_from_pb;

fn commify(value: u64) -> String {
    value.to_formatted_string(&Locale::en)
}

fn commify_f64(value: f64) -> String {
    commify(value as u64)
}

fn timeify(mut seconds: u64) -> String {
    let mut parts = Vec::new();
    let days = seconds / (3600 * 24);
    seconds %= 3600 * 24;
    let hours = seconds / 3600;
    seconds %= 3600;
    let minutes = seconds / 60;
    seconds %= 60;

    if days != 0 {
        parts.push(format!("{} days", days));
    }
    if hours != 0 {
        parts.push(format!("{} hours", hours));
    }
    if minutes != 0 {
        parts.push(format!("{} minutes", minutes));
    }
    if seconds != 0 {
        parts.push(format!("{} seconds", seconds));
    }

    parts.join(" ")
}

fn coin_print_status(cf: &pb::Coinstatus) {
    let results = result_array_from_pb(&cf.flips);

    let total_flips = commify(cf.total_flips as u64);
    let fps = commify_f64(cf.flips_per_second);
    let width = total_flips.len().max(fps.len());

    println!("Total coins flipped: {:>width$}", total_flips, width = width);
    println!("Coins per second:    {:>width$}", fps, width = width);
    println!();

    // Connected clients sorted by speed (descending)
    let mut stats: Vec<_> = cf.stats.iter().collect();
    stats.sort_by(|a, b| b.flips_per_second.cmp(&a.flips_per_second));

    if !stats.is_empty() {
        println!("Connected clients:");
        let max_len = stats
            .iter()
            .map(|s| commify(s.flips_per_second as u64).len())
            .max()
            .unwrap_or(0);

        for s in &stats {
            let speed = commify(s.flips_per_second as u64);
            println!(
                "{:08x}: {:>width$} cps",
                s.hash as u64,
                speed,
                width = max_len
            );
        }
        println!();
    }

    // Milestone calculation
    if cf.total_flips > 0 && cf.flips_per_second > 0.0 {
        let milestone = (cf.total_flips as f64).log10();
        let rest = 10f64.powf(milestone.ceil()) - cf.total_flips as f64;
        let remaining = rest / cf.flips_per_second;

        println!(
            "Time remaining to next milestone: {}",
            timeify(remaining as u64)
        );
        println!();
    }

    // 4-column x 32-row table
    let mut columns: [Vec<String>; 4] = [vec![], vec![], vec![], vec![]];
    for i in 0..128 {
        columns[i / 32].push(commify(results[i]));
    }

    let max_widths: [usize; 4] = [
        columns[0].iter().map(|s| s.len()).max().unwrap_or(1),
        columns[1].iter().map(|s| s.len()).max().unwrap_or(1),
        columns[2].iter().map(|s| s.len()).max().unwrap_or(1),
        columns[3].iter().map(|s| s.len()).max().unwrap_or(1),
    ];

    for i in 0..32 {
        for j in 0..4 {
            let pos = i + 32 * j + 1;
            print!(
                "{:>3}: {:>width$}",
                pos,
                columns[j][i],
                width = max_widths[j]
            );
            if j < 3 {
                print!("        ");
            }
        }
        println!();
    }
}

pub async fn coin_status(
    server_address: String,
    export: bool,
) -> Result<(), Box<dyn std::error::Error>> {
    let endpoint = format!("http://{}:50051", server_address);
    let mut client = CoinFlipperClient::connect(endpoint).await?;

    let response = client
        .get_status(pb::StatusRequest {})
        .await?
        .into_inner();

    if export {
        use prost::Message;
        let data = response.encode_to_vec();
        std::io::stdout().write_all(&data)?;
    } else {
        coin_print_status(&response);
    }

    Ok(())
}
