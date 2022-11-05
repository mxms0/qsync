use std::collections::VecDeque;
use std::fs::{self};
use std::io::{self, Error};
use std::path::Path;
use std::path::PathBuf;
use std::time::SystemTime;

#[derive(Debug)]
pub struct FileInfo {
    pub file_path : PathBuf,
    pub file_size : u64,
    pub is_dir : bool,
    pub modification_time : SystemTime,
}

pub fn walk_files<F>(path: &Path, mut touch: F) -> io::Result<()>
where
    F: FnMut(io::Result<&mut FileInfo>),
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
                            match entry.metadata() {
                                Err(error) => {
                                    println!("{:?}", error);
                                    touch(Err(error));
                                }
                                Ok(metadata) => {
                                    let mut info = FileInfo {
                                        file_path: entry.path(),
                                        file_size: metadata.len(),
                                        is_dir : metadata.is_dir(),
                                        modification_time: metadata.modified().unwrap_or(SystemTime::now()),
                                    };
                                    touch(Ok(&mut info));
                                }
                            }
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
