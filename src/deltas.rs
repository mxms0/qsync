use std::path::Path;

pub enum Bottleneck {
    Network,
    Disk,
    CPU,
}

// Resolution is the suggested method for how to resolve a difference between
// sender and receiver.
pub enum Resolution {
    DirectoryComplete,
    DirectoryCompleteCompressed,

    DirectoryDelta,
    DirectoryDeltaCompressed,

    FileComplete,
    FileDelta,

    FileCompleteCompressed,
    FileDeltaCompressed,
}

// Best-effort decision on what method to use to resolve a conflict. Very
// primitive today. For the future we may want to decide based on:
// - Partial hashes from the remote
// - Size of the directory locally
// - Size of the directory on the remote
// I assume we'll have some info indicating that there was a conflict, so that
// info should eventually get propgated to here.
pub fn resolution_method(bn: Bottleneck, f1: &Path) -> Resolution {
    if f1.is_dir() {
        return match bn {
            Bottleneck::Disk => Resolution::DirectoryDelta,
            Bottleneck::Network => Resolution::DirectoryDeltaCompressed,
            Bottleneck::CPU => Resolution::DirectoryDelta,
        };
    } else {
        return match bn {
            Bottleneck::Disk => Resolution::FileDelta,
            Bottleneck::Network => Resolution::FileDeltaCompressed,
            Bottleneck::CPU => Resolution::FileComplete,
        };
    }
}
