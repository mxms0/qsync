pub mod files;
pub mod auth;
pub mod server;

use std::path::Path;

use crate::files::walk_files;

fn main() {
    let path = Path::new(".");
    walk_files(path, |p| println!("{:?}", p)).expect("walk files failed!");
}
