use std::collections::VecDeque;
use std::fs::{self};
use std::io::{self, Error};
use std::path::Path;
use std::path::PathBuf;

pub fn walk_files<F>(path: &Path, mut touch: F) -> io::Result<()>
where
    F: FnMut(io::Result<&PathBuf>),
{
    if !path.exists() {
        return Err(Error::from(io::ErrorKind::NotFound));
    }
    if !path.is_dir() {
        return Err(Error::from(io::ErrorKind::InvalidInput));
    }

    let mut paths = VecDeque::new();
    paths.push_back(PathBuf::from(path));

    while let Some(current_path) = paths.pop_front() {
        match fs::read_dir(current_path) {
            Err(why) => {
                println!("{:?}", why.kind());
                touch(Err(why));
            }
            Ok(entries) => {
                for entry in entries {
                    match entry {
                        Err(error) => {
                            println!("{:?}", error);
                            touch(Err(error));
                        }
                        Ok(entry) => {
                            touch(Ok(&entry.path()));
                            if entry.path().is_dir() {
                                paths.push_back(entry.path());
                            }
                        }
                    };
                }
            }
        }
    }
    Ok(())
}
