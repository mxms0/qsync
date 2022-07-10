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

// rsync syntax allows you to specify locations like
// hostname.tld:/path/to/thing, or just /path/to/thing in the case of it being
// local. Return Some("hostname.tld") if it exists.
fn get_remote_from_location(location: &String) -> Option<String> {
    // TODO: Check if the colon is escaped.
    let components: Vec<&str> = location.split(':').collect();

    if components.len() > 1 {
        return Some(components[0].to_string());
    }

    None
}

fn main() {
    let args = QsyncArguments::parse();
    println!("source: {:?}", args.source);
    println!("destin: {:?}", args.destination);

    let source_remote = get_remote_from_location(&args.source);
    let destination_remote = get_remote_from_location(&args.destination);

    if source_remote.is_some() { /* Check if host is reachable */ }
    if destination_remote.is_some() { /* Check if host is reachable */ }

    walk_files(Path::new(&args.source), |p| println!("{:?}", p)).expect("walk files failed!");
    walk_files(Path::new(&args.destination), |p| println!("{:?}", p)).expect("walk files failed!");
}
