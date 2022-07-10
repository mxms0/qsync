pub mod auth;
pub mod deltas;
pub mod files;

use std::path::Path;

use clap::Parser;

use crate::files::walk_files;

#[derive(Parser, Debug)]
struct QsyncArguments {
    /// Source to synchronize with
    source: String,
    /// Destination to synchronize to
    destination: String,
}

fn main() {
    let args = QsyncArguments::parse();
    println!("source: {:?}", args.source);
    println!("destin: {:?}", args.destination);
    walk_files(Path::new(&args.source), |p| println!("{:?}", p)).expect("walk files failed!");
    walk_files(Path::new(&args.destination), |p| println!("{:?}", p)).expect("walk files failed!");
}
